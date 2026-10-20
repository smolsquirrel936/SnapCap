#include <QtTest>
#include <QElapsedTimer>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLVersionFunctionsFactory>
#include <QOpenGLFunctions_3_3_Core>
#include <QSurfaceFormat>

#include <algorithm>
#include <memory>
#include <vector>

#include "core/render/Raster.h"
#include "core/render/GpuBlur.h"

// Stage 7 measurement: how close is the CPU redaction path to the plan's
// "<16 ms re-render" interactivity target? Both ops re-run on every repaint while
// a Mosaic/Blur shape is being dragged, so per-call cost IS the frame budget.
//
// Deliberately NOT registered with ctest: it asserts nothing about wall-clock
// (that would be flaky across machines) and the 4K cases take seconds. Build and
// run it by hand when the numbers matter:  build\tst_render_bench.exe
//
// Parameters mirror core/annotation/Annotation.cpp:317,338 --
//   block  = max(6, redactStrength * 3 * scale)
//   radius = max(2, redactStrength * 2 * scale)
// with redactStrength 6 = PaintBar default, 20 = slider max, scale 2 = 200% DPI.
class TstRenderBench : public QObject
{
    Q_OBJECT
private slots:
    void mosaicCost();
    void blurCost();
    void blurAlgorithmAlternative();
    void gpuAvailability();
    void gpuCost();
    void shippedGpuBlurMatchesCpu();
    void summary();
};

namespace {

constexpr double kBudgetMs = 16.0;      // the Implementation-Plan Stage 7 target

// Deterministic high-frequency noise: worst case for both ops (no early-out,
// no cache-friendly flat regions) and identical on every run/machine.
QImage noise(int w, int h)
{
    QImage img(w, h, QImage::Format_ARGB32);
    quint32 s = 0x9E3779B9u;
    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;   // xorshift32
            line[x] = qRgba(s & 0xFF, (s >> 8) & 0xFF, (s >> 16) & 0xFF, 255);
        }
    }
    return img;
}

// Median of `reps` runs. Heavy cases pass reps=1 to keep total runtime sane.
template <typename F>
double medianMs(F&& fn, int reps)
{
    std::vector<double> ms;
    ms.reserve(reps);
    for (int i = 0; i < reps; ++i) {
        QElapsedTimer t;
        t.start();
        const QImage out = fn();
        ms.push_back(t.nsecsElapsed() / 1e6);
        Q_ASSERT(!out.isNull());
    }
    std::sort(ms.begin(), ms.end());
    return ms[ms.size() / 2];
}

struct Row { QString label; double ms; };
std::vector<Row> g_rows;

void record(const QString& label, double ms)
{
    g_rows.push_back({label, ms});
    qInfo("%-46s %9.2f ms  %s", qPrintable(label), ms,
          ms <= kBudgetMs ? "<= 16ms" : "OVER");
}

} // namespace

void TstRenderBench::mosaicCost()
{
    const QImage k4 = noise(3840, 2160);          // full 4K screen
    const QImage reg = noise(960, 540);           // a typical redacted region

    record("mosaic  960x540   block 18 (default)",
           medianMs([&] { return render::mosaic(reg, 18); }, 5));
    record("mosaic  960x540   block 120 (max)",
           medianMs([&] { return render::mosaic(reg, 120); }, 5));
    record("mosaic  3840x2160 block 18 (default)",
           medianMs([&] { return render::mosaic(k4, 18); }, 3));
    record("mosaic  3840x2160 block 120 (max)",
           medianMs([&] { return render::mosaic(k4, 120); }, 3));

    QCOMPARE(render::mosaic(reg, 18).size(), reg.size());
}

void TstRenderBench::blurCost()
{
    const QImage k4 = noise(3840, 2160);
    const QImage reg = noise(960, 540);

    record("blur    960x540   radius 12 (default)",
           medianMs([&] { return render::gaussianBlur(reg, 12); }, 5));
    record("blur    960x540   radius 80 (max)",
           medianMs([&] { return render::gaussianBlur(reg, 80); }, 3));
    record("blur    3840x2160 radius 12 (default)",
           medianMs([&] { return render::gaussianBlur(k4, 12); }, 1));
    record("blur    3840x2160 radius 80 (max)",
           medianMs([&] { return render::gaussianBlur(k4, 80); }, 1));

    QCOMPARE(render::gaussianBlur(reg, 12).size(), reg.size());
}

namespace {

// Largest per-channel difference between two same-size images.
int maxChannelDelta(const QImage& x, const QImage& y)
{
    int worst = 0;
    for (int j = 0; j < x.height(); ++j) {
        const QRgb* lx = reinterpret_cast<const QRgb*>(x.constScanLine(j));
        const QRgb* ly = reinterpret_cast<const QRgb*>(y.constScanLine(j));
        for (int i = 0; i < x.width(); ++i) {
            worst = std::max({worst,
                              std::abs(qRed(lx[i])   - qRed(ly[i])),
                              std::abs(qGreen(lx[i]) - qGreen(ly[i])),
                              std::abs(qBlue(lx[i])  - qBlue(ly[i]))});
        }
    }
    return worst;
}

} // namespace

// The shipped CPU blur (render::boxBlur) against the true Gaussian it replaced.
// This slot was originally a *prototype* living in this file, written to answer
// "is the CPU path slow because it has no GPU, or because of the algorithm?"
// before committing to the GPU work. The answer was "the algorithm", the
// prototype was promoted into core/render, and this now measures the real thing --
// so there is exactly one box-blur implementation on the CPU side to keep correct.
//
// Note the numbers here shifted slightly when the prototype was promoted:
// render::boxBlur rounds (`(sum+n/2)/n`) where this prototype truncated, which
// fixed a 4-level systematic darkening across its six passes.
void TstRenderBench::blurAlgorithmAlternative()
{
    const QImage k4 = noise(3840, 2160);
    const QImage reg = noise(960, 540);

    record("box3    960x540   radius 12 (default)",
           medianMs([&] { return render::boxBlur(reg, 12); }, 5));
    record("box3    960x540   radius 80 (max)",
           medianMs([&] { return render::boxBlur(reg, 80); }, 5));
    record("box3    3840x2160 radius 12 (default)",
           medianMs([&] { return render::boxBlur(k4, 12); }, 3));
    record("box3    3840x2160 radius 80 (max)",
           medianMs([&] { return render::boxBlur(k4, 80); }, 3));

    // Fidelity: how visible is the approximation on a smooth gradient (a much
    // fairer subject than noise, which hides blur differences)?
    QImage grad(400, 200, QImage::Format_ARGB32);
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 400; ++x)
            grad.setPixelColor(x, y, x < 200 ? QColor(20, 40, 60) : QColor(230, 210, 190));
    for (int r : {12, 80})
        qInfo("fidelity: box3 vs gaussian, radius %-3d -> max per-channel delta %d",
              r, maxChannelDelta(render::boxBlur(grad, r), render::gaussianBlur(grad, r)));
}

namespace {

// ---- Reference-only GPU path, NOT a shipped code path -----------------------
// The Stage 7 bullet proposes an offscreen QOpenGLContext + FBO shader path. None
// exists in core/render, so this prototype is what makes "CPU vs GPU" measurable
// at all. Deliberately measures the WHOLE round trip (QImage -> texture -> passes
// -> readback -> QImage), because render::mosaic/gaussianBlur return a QImage that
// the caller draws with QPainter -- kernel-only timings would flatter the GPU and
// answer a question nobody is asking. glFinish() bounds each phase, otherwise we
// would be timing command submission rather than execution.
//
// Textures/FBOs are cached across calls of the same size, which is what a real
// implementation does while a redaction shape is being dragged, so the reported
// figure is steady-state interactive cost, not one-shot cold cost.

const char* kVertSrc = R"(#version 330 core
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";

// Separable Gaussian; clamps at edges exactly like render::blurPass.
const char* kBlurSrc = R"(#version 330 core
uniform sampler2D uSrc;
uniform int   uRadius;
uniform int   uHorizontal;
uniform int   uW;
uniform int   uH;
uniform float uKernel[161];
out vec4 fragColor;
void main() {
    ivec2 c = ivec2(gl_FragCoord.xy);
    vec4 acc = vec4(0.0);
    for (int t = -uRadius; t <= uRadius; ++t) {
        ivec2 s = c;
        if (uHorizontal == 1) s.x = clamp(c.x + t, 0, uW - 1);
        else                  s.y = clamp(c.y + t, 0, uH - 1);
        acc += texelFetch(uSrc, s, 0) * uKernel[t + uRadius];
    }
    fragColor = acc;
})";

// Block average into a 1-texel-per-cell target; edge cells average fewer texels,
// matching render::mosaic's clipped-block behaviour.
const char* kMosaicDownSrc = R"(#version 330 core
uniform sampler2D uSrc;
uniform int   uBlock;
uniform int   uW;
uniform int   uH;
out vec4 fragColor;
void main() {
    ivec2 o = ivec2(gl_FragCoord.xy) * uBlock;
    vec4 acc = vec4(0.0);
    int n = 0;
    for (int y = 0; y < uBlock; ++y) {
        int sy = o.y + y;
        if (sy >= uH) break;
        for (int x = 0; x < uBlock; ++x) {
            int sx = o.x + x;
            if (sx >= uW) break;
            acc += texelFetch(uSrc, ivec2(sx, sy), 0);
            ++n;
        }
    }
    fragColor = acc / float(n);
})";

const char* kMosaicUpSrc = R"(#version 330 core
uniform sampler2D uSmall;
uniform int uBlock;
out vec4 fragColor;
void main() {
    fragColor = texelFetch(uSmall, ivec2(gl_FragCoord.xy) / uBlock, 0);
})";

class GpuRaster
{
public:
    bool init(QString* why);
    QString renderer() const { return m_renderer; }
    bool isSoftware() const
    {
        const QString r = m_renderer.toLower();
        return r.contains("llvmpipe") || r.contains("softpipe")
            || r.contains("gdi generic") || r.contains("basic render");
    }

    QImage mosaic(const QImage& src, int block);
    QImage gaussianBlur(const QImage& src, int radius);

    double upMs = 0, renderMs = 0, backMs = 0;      // last call's phase breakdown

private:
    void uploadSource(const QImage& src);
    void ensureFbo(std::unique_ptr<QOpenGLFramebufferObject>& fbo, QSize sz);
    void drawFullscreen() { m_f->glDrawArrays(GL_TRIANGLES, 0, 3); }

    std::unique_ptr<QOffscreenSurface> m_surface;
    std::unique_ptr<QOpenGLContext> m_ctx;
    QOpenGLFunctions_3_3_Core* m_f = nullptr;
    QOpenGLShaderProgram m_blur, m_mosDown, m_mosUp;
    std::unique_ptr<QOpenGLFramebufferObject> m_fboA, m_fboB, m_fboSmall;
    GLuint m_vao = 0, m_tex = 0;
    QSize m_texSize;
    QString m_renderer;
};

bool buildProgram(QOpenGLShaderProgram& p, const char* frag, QString* why)
{
    if (!p.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertSrc)
        || !p.addShaderFromSourceCode(QOpenGLShader::Fragment, frag)
        || !p.link()) {
        *why = p.log();
        return false;
    }
    return true;
}

bool GpuRaster::init(QString* why)
{
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);

    m_surface = std::make_unique<QOffscreenSurface>();
    m_surface->setFormat(fmt);
    m_surface->create();
    if (!m_surface->isValid()) { *why = "offscreen surface not valid"; return false; }

    m_ctx = std::make_unique<QOpenGLContext>();
    m_ctx->setFormat(fmt);
    if (!m_ctx->create())                     { *why = "QOpenGLContext::create failed"; return false; }
    if (!m_ctx->makeCurrent(m_surface.get())) { *why = "makeCurrent failed"; return false; }

    m_f = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_3_Core>(m_ctx.get());
    if (!m_f) { *why = "no OpenGL 3.3 core functions"; return false; }
    m_f->initializeOpenGLFunctions();
    m_renderer = QString::fromLatin1(
        reinterpret_cast<const char*>(m_f->glGetString(GL_RENDERER)));

    if (!buildProgram(m_blur, kBlurSrc, why)) return false;
    if (!buildProgram(m_mosDown, kMosaicDownSrc, why)) return false;
    if (!buildProgram(m_mosUp, kMosaicUpSrc, why)) return false;

    m_f->glGenVertexArrays(1, &m_vao);
    m_f->glBindVertexArray(m_vao);                 // core profile requires a bound VAO
    m_f->glGenTextures(1, &m_tex);
    m_f->glBindTexture(GL_TEXTURE_2D, m_tex);
    m_f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    m_f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    m_f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return true;
}

// ARGB32 is B,G,R,A in memory on little-endian, so GL_BGRA/UNSIGNED_BYTE uploads
// it with no channel shuffling and no format conversion.
void GpuRaster::uploadSource(const QImage& src)
{
    const QImage img = src.convertToFormat(QImage::Format_ARGB32);
    m_f->glBindTexture(GL_TEXTURE_2D, m_tex);
    m_f->glPixelStorei(GL_UNPACK_ROW_LENGTH, int(img.bytesPerLine() / 4));
    if (img.size() != m_texSize) {
        m_f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.width(), img.height(), 0,
                          GL_BGRA, GL_UNSIGNED_BYTE, img.constBits());
        m_texSize = img.size();
    } else {
        m_f->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, img.width(), img.height(),
                             GL_BGRA, GL_UNSIGNED_BYTE, img.constBits());
    }
    m_f->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

void GpuRaster::ensureFbo(std::unique_ptr<QOpenGLFramebufferObject>& fbo, QSize sz)
{
    if (!fbo || fbo->size() != sz)
        fbo = std::make_unique<QOpenGLFramebufferObject>(sz, QOpenGLFramebufferObject::NoAttachment);
}

QImage GpuRaster::mosaic(const QImage& src, int block)
{
    const int w = src.width(), h = src.height();
    QElapsedTimer t;

    t.start();
    uploadSource(src);
    m_f->glFinish();
    upMs = t.nsecsElapsed() / 1e6;

    t.restart();
    const int cw = (w + block - 1) / block, ch = (h + block - 1) / block;
    ensureFbo(m_fboSmall, QSize(cw, ch));
    ensureFbo(m_fboA, QSize(w, h));

    m_fboSmall->bind();
    m_f->glViewport(0, 0, cw, ch);
    m_mosDown.bind();
    m_f->glActiveTexture(GL_TEXTURE0);
    m_f->glBindTexture(GL_TEXTURE_2D, m_tex);
    m_mosDown.setUniformValue("uSrc", 0);
    m_mosDown.setUniformValue("uBlock", block);
    m_mosDown.setUniformValue("uW", w);
    m_mosDown.setUniformValue("uH", h);
    drawFullscreen();

    m_fboA->bind();
    m_f->glViewport(0, 0, w, h);
    m_mosUp.bind();
    m_f->glBindTexture(GL_TEXTURE_2D, m_fboSmall->texture());
    m_mosUp.setUniformValue("uSmall", 0);
    m_mosUp.setUniformValue("uBlock", block);
    drawFullscreen();
    m_f->glFinish();
    renderMs = t.nsecsElapsed() / 1e6;

    t.restart();
    const QImage out = m_fboA->toImage(false).convertToFormat(QImage::Format_ARGB32);
    backMs = t.nsecsElapsed() / 1e6;
    return out;
}

QImage GpuRaster::gaussianBlur(const QImage& src, int radius)
{
    const int w = src.width(), h = src.height();
    QElapsedTimer t;

    // Same kernel the CPU path builds, so any delta is arithmetic, not shape.
    const double sigma = qMax(0.5, radius / 2.0);
    std::vector<float> kernel(2 * radius + 1);
    float sum = 0.0f;
    for (int i = -radius; i <= radius; ++i) {
        const float v = float(std::exp(-(i * i) / (2.0 * sigma * sigma)));
        kernel[i + radius] = v;
        sum += v;
    }
    for (float& v : kernel) v /= sum;

    t.start();
    uploadSource(src);
    m_f->glFinish();
    upMs = t.nsecsElapsed() / 1e6;

    t.restart();
    ensureFbo(m_fboA, QSize(w, h));
    ensureFbo(m_fboB, QSize(w, h));
    m_blur.bind();
    m_blur.setUniformValue("uSrc", 0);
    m_blur.setUniformValue("uRadius", radius);
    m_blur.setUniformValue("uW", w);
    m_blur.setUniformValue("uH", h);
    m_blur.setUniformValueArray("uKernel", kernel.data(), int(kernel.size()), 1);
    m_f->glActiveTexture(GL_TEXTURE0);
    m_f->glViewport(0, 0, w, h);

    m_fboA->bind();                                  // horizontal
    m_f->glBindTexture(GL_TEXTURE_2D, m_tex);
    m_blur.setUniformValue("uHorizontal", 1);
    drawFullscreen();

    m_fboB->bind();                                  // vertical
    m_f->glBindTexture(GL_TEXTURE_2D, m_fboA->texture());
    m_blur.setUniformValue("uHorizontal", 0);
    drawFullscreen();
    m_f->glFinish();
    renderMs = t.nsecsElapsed() / 1e6;

    t.restart();
    const QImage out = m_fboB->toImage(false).convertToFormat(QImage::Format_ARGB32);
    backMs = t.nsecsElapsed() / 1e6;
    return out;
}

GpuRaster g_gpu;
bool g_gpuReady = false;

} // namespace

void TstRenderBench::gpuAvailability()
{
    QString why;
    g_gpuReady = g_gpu.init(&why);
    if (!g_gpuReady) {
        qInfo("GPU path UNAVAILABLE: %s", qPrintable(why));
        QSKIP("no usable OpenGL 3.3 context -- GPU cases cannot run here");
    }
    qInfo("GL_RENDERER: %s", qPrintable(g_gpu.renderer()));
    if (g_gpu.isSoftware()) {
        qWarning("This is a SOFTWARE rasterizer -- the numbers below are NOT a GPU "
                 "measurement, and are exactly the case the Stage 7 probe must reject.");
    }
}

void TstRenderBench::gpuCost()
{
    if (!g_gpuReady)
        QSKIP("GPU path unavailable");

    const QImage k4 = noise(3840, 2160);
    const QImage reg = noise(960, 540);

    struct Case { const char* label; const QImage* img; bool blur; int param; int reps; };
    const Case cases[] = {
        {"gpu mos 960x540   block 18 (default)",  &reg, false, 18,  5},
        {"gpu mos 960x540   block 120 (max)",     &reg, false, 120, 5},
        {"gpu mos 3840x2160 block 18 (default)",  &k4,  false, 18,  3},
        {"gpu mos 3840x2160 block 120 (max)",     &k4,  false, 120, 3},
        {"gpu blur 960x540   radius 12 (default)", &reg, true,  12,  5},
        {"gpu blur 960x540   radius 80 (max)",     &reg, true,  80,  5},
        {"gpu blur 3840x2160 radius 12 (default)", &k4,  true,  12,  3},
        {"gpu blur 3840x2160 radius 80 (max)",     &k4,  true,  80,  3},
    };

    for (const Case& c : cases) {
        const double ms = medianMs([&] {
            return c.blur ? g_gpu.gaussianBlur(*c.img, c.param)
                          : g_gpu.mosaic(*c.img, c.param);
        }, c.reps);
        record(QString::fromLatin1(c.label), ms);
        qInfo("      phases: upload %.2f  render %.2f  readback %.2f ms",
              g_gpu.upMs, g_gpu.renderMs, g_gpu.backMs);
    }

    // Correctness: the max-per-channel-delta check the Stage 7 plan calls for in
    // place of bit-exact goldens on a GPU path. This is NOT ceremony -- it caught a
    // real bug: setUniformValue(name, QPoint) sends a float vec2 (glUniform2fv), so
    // an `ivec2` size uniform silently stayed (0,0) and every sample clamped to
    // texel 0. A uniform-colour image hides that completely; only a non-uniform one
    // exposes it. Hence a split image is checked too, not just noise.
    QImage vsplit(200, 100, QImage::Format_ARGB32);
    for (int y = 0; y < 100; ++y)
        for (int x = 0; x < 200; ++x)
            vsplit.setPixelColor(x, y, y < 50 ? QColor(10, 20, 30) : QColor(200, 210, 220));

    const int dMosaic = maxChannelDelta(g_gpu.mosaic(reg, 18), render::mosaic(reg, 18));
    const int dBlur   = maxChannelDelta(g_gpu.gaussianBlur(reg, 12),
                                        render::gaussianBlur(reg, 12));
    const int dSplit  = maxChannelDelta(g_gpu.gaussianBlur(vsplit, 4),
                                        render::gaussianBlur(vsplit, 4));
    qInfo("fidelity: gpu vs cpu, mosaic block 18 -> max per-channel delta %d", dMosaic);
    qInfo("fidelity: gpu vs cpu, blur radius 12  -> max per-channel delta %d", dBlur);
    qInfo("fidelity: gpu vs cpu, blur on split   -> max per-channel delta %d", dSplit);

    // Tolerance, not equality: GPU float math vs the CPU path's integer truncation
    // (mosaic) / lround (blur) legitimately differ in the last bit or two.
    QVERIFY2(dMosaic <= 2, qPrintable(QString("mosaic delta %1").arg(dMosaic)));
    QVERIFY2(dBlur   <= 2, qPrintable(QString("blur delta %1").arg(dBlur)));
    QVERIFY2(dSplit  <= 2, qPrintable(QString("split delta %1").arg(dSplit)));
}

// The shipped path, not the reference prototype above: render::blur() in Auto mode
// must produce the same picture as the CPU boxBlur it falls back to. This is the
// only place that exercises core/render/GpuBlur at all -- tst_render is
// QTEST_GUILESS_MAIN, so the GPU there is unreachable by construction.
void TstRenderBench::shippedGpuBlurMatchesCpu()
{
    render::setBlurMode(render::BlurMode::Auto);
    const QImage probe = render::blur(noise(64, 64), 4);
    Q_UNUSED(probe)                                  // first call triggers the probe
    const QString status = render::gpuBlurStatus();
    qInfo("shipped GPU blur status: %s", qPrintable(status));

    if (render::isSoftwareRenderer(status) || status.contains(QLatin1Char(' '))) {
        // A status containing spaces is a failure reason ("software rasterizer
        // (llvmpipe)", "no QGuiApplication"), not an adapter name we can trust.
        // Adapter names contain spaces too, so this is only a heuristic -- the
        // delta assertions below are what actually decide correctness.
        qInfo("(if that reads as a failure reason, the CPU fallback is under test)");
    }

    // Non-uniform inputs on purpose: a flat colour blurs to itself and would pass
    // even if every sample clamped to texel 0 (the bug the reference path hit).
    const QImage noiseImg = noise(320, 200);
    QImage vsplit(200, 100, QImage::Format_ARGB32);
    for (int y = 0; y < 100; ++y)
        for (int x = 0; x < 200; ++x)
            vsplit.setPixelColor(x, y, y < 50 ? QColor(10, 20, 30) : QColor(200, 210, 220));
    QImage hsplit(200, 100, QImage::Format_ARGB32);   // catches an axis/flip swap
    for (int y = 0; y < 100; ++y)
        for (int x = 0; x < 200; ++x)
            hsplit.setPixelColor(x, y, x < 100 ? QColor(240, 30, 30) : QColor(30, 30, 240));

    for (const auto& [img, label] : {std::pair<QImage, const char*>{noiseImg, "noise"},
                                     {vsplit, "vertical split"},
                                     {hsplit, "horizontal split"}}) {
        for (const int r : {2, 12, 80}) {
            render::setBlurMode(render::BlurMode::Auto);
            const QImage gpu = render::blur(img, r);
            render::setBlurMode(render::BlurMode::Cpu);
            const QImage cpu = render::blur(img, r);

            QCOMPARE(gpu.size(), cpu.size());
            const int d = maxChannelDelta(gpu, cpu);
            qInfo("shipped blur: %-18s radius %-3d -> max per-channel delta %d", label, r, d);
            // +-1 is the honest bound: the CPU sums integers and truncates, the GPU
            // averages in float and rounds on write to RGBA8. Anything larger means
            // the two backends are running different passes, which is the whole
            // thing the "both paths run 3x box" decision exists to prevent.
            QVERIFY2(d <= 1, qPrintable(QStringLiteral("%1 r=%2 delta %3")
                                            .arg(QLatin1String(label)).arg(r).arg(d)));
        }
    }
    render::setBlurMode(render::BlurMode::Cpu);
}

void TstRenderBench::summary()
{
    int over = 0;
    for (const Row& r : g_rows)
        if (r.ms > kBudgetMs) ++over;
    qInfo("---- %d of %d cases exceed the %.0f ms interactive budget ----",
          over, int(g_rows.size()), kBudgetMs);
    QVERIFY(!g_rows.empty());
}

// QTEST_MAIN (not GUILESS): an OpenGL context needs a QGuiApplication.
QTEST_MAIN(TstRenderBench)
#include "tst_render_bench.moc"
