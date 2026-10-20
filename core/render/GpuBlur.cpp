#include "core/render/GpuBlur.h"
#include "core/render/Raster.h"

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVersionFunctionsFactory>
#include <QSurfaceFormat>
#include <QThread>
#include <QLoggingCategory>

#include <memory>

namespace render {

namespace {

// gl_VertexID trick: one 3-vertex triangle covering the viewport, so no vertex
// buffer is needed at all.
const char* kVertSrc = R"(#version 330 core
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";

// One box pass along one axis. Edges clamp, matching boxPass() on the CPU.
const char* kBoxSrc = R"(#version 330 core
uniform sampler2D uSrc;
uniform int uRadius;
uniform int uHorizontal;
uniform int uW;
uniform int uH;
out vec4 fragColor;
void main() {
    ivec2 c = ivec2(gl_FragCoord.xy);
    vec4 acc = vec4(0.0);
    for (int t = -uRadius; t <= uRadius; ++t) {
        ivec2 s = c;
        if (uHorizontal == 1) s.x = clamp(c.x + t, 0, uW - 1);
        else                  s.y = clamp(c.y + t, 0, uH - 1);
        acc += texelFetch(uSrc, s, 0);
    }
    fragColor = acc / float(2 * uRadius + 1);
})";

class GpuBlurContext
{
public:
    // Returns null on any failure; disables itself permanently after the first one.
    QImage blur(const QImage& src, int radius);
    QString status() const { return m_status; }

private:
    bool ensureReady();
    bool init(QString* why);
    void uploadSource(const QImage& src);
    void ensureFbo(std::unique_ptr<QOpenGLFramebufferObject>& fbo, QSize sz);
    void drawFullscreen() { m_f->glDrawArrays(GL_TRIANGLES, 0, 3); }
    QImage fail(const QString& why);

    enum class State { Untried, Ready, Unavailable };
    State m_state = State::Untried;
    QString m_status;

    std::unique_ptr<QOffscreenSurface> m_surface;
    std::unique_ptr<QOpenGLContext> m_ctx;
    QOpenGLFunctions_3_3_Core* m_f = nullptr;
    QOpenGLShaderProgram m_box;
    std::unique_ptr<QOpenGLFramebufferObject> m_fboA, m_fboB;
    GLuint m_vao = 0, m_tex = 0;
    QSize m_texSize;
};

QImage GpuBlurContext::fail(const QString& why)
{
    m_state = State::Unavailable;
    m_status = why;
    qWarning("[render] GPU blur unavailable (%s) - using the CPU box blur",
             qPrintable(why));
    // Drop the GL objects; we will never retry.
    m_fboA.reset();
    m_fboB.reset();
    m_ctx.reset();
    m_surface.reset();
    m_f = nullptr;
    return QImage();
}

bool GpuBlurContext::init(QString* why)
{
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);

    m_surface = std::make_unique<QOffscreenSurface>();
    m_surface->setFormat(fmt);
    m_surface->create();
    if (!m_surface->isValid()) { *why = QStringLiteral("offscreen surface not valid"); return false; }

    m_ctx = std::make_unique<QOpenGLContext>();
    m_ctx->setFormat(fmt);
    if (!m_ctx->create())
        { *why = QStringLiteral("QOpenGLContext::create failed"); return false; }
    if (!m_ctx->makeCurrent(m_surface.get()))
        { *why = QStringLiteral("makeCurrent failed"); return false; }

    m_f = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_3_Core>(m_ctx.get());
    if (!m_f) { *why = QStringLiteral("no OpenGL 3.3 core functions"); return false; }
    m_f->initializeOpenGLFunctions();

    const QString renderer = QString::fromLatin1(
        reinterpret_cast<const char*>(m_f->glGetString(GL_RENDERER)));
    // The check that actually matters -- see isSoftwareRenderer()'s header note.
    if (isSoftwareRenderer(renderer)) {
        *why = QStringLiteral("software rasterizer (%1)").arg(renderer);
        return false;
    }

    if (!m_box.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertSrc)
        || !m_box.addShaderFromSourceCode(QOpenGLShader::Fragment, kBoxSrc)
        || !m_box.link()) {
        *why = QStringLiteral("shader: %1").arg(m_box.log().simplified());
        return false;
    }

    m_f->glGenVertexArrays(1, &m_vao);
    m_f->glBindVertexArray(m_vao);              // core profile requires a bound VAO
    m_f->glGenTextures(1, &m_tex);
    m_f->glBindTexture(GL_TEXTURE_2D, m_tex);
    m_f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    m_f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    m_f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    m_status = renderer;
    qInfo("[render] GPU blur enabled on %s", qPrintable(renderer));
    return true;
}

bool GpuBlurContext::ensureReady()
{
    if (m_state == State::Ready)       return true;
    if (m_state == State::Unavailable) return false;

    // A GL context needs a QGuiApplication and the GUI thread. Both are guaranteed
    // in the app but not in a headless test, so check rather than crash.
    if (!qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        fail(QStringLiteral("no QGuiApplication"));
        return false;
    }
    if (QThread::currentThread() != QCoreApplication::instance()->thread()) {
        fail(QStringLiteral("not the GUI thread"));
        return false;
    }

    QString why;
    if (!init(&why)) {
        fail(why);
        return false;
    }
    m_state = State::Ready;
    return true;
}

// ARGB32 is B,G,R,A in memory on little-endian, so GL_BGRA/UNSIGNED_BYTE uploads
// it with no channel shuffling and no format conversion.
void GpuBlurContext::uploadSource(const QImage& img)
{
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

void GpuBlurContext::ensureFbo(std::unique_ptr<QOpenGLFramebufferObject>& fbo, QSize sz)
{
    if (!fbo || fbo->size() != sz)
        fbo = std::make_unique<QOpenGLFramebufferObject>(sz, QOpenGLFramebufferObject::NoAttachment);
}

QImage GpuBlurContext::blur(const QImage& src, int radius)
{
    if (!ensureReady())
        return QImage();
    if (!m_ctx->makeCurrent(m_surface.get()))
        return fail(QStringLiteral("makeCurrent failed"));

    const QImage in = src.convertToFormat(QImage::Format_ARGB32);
    const int w = in.width(), h = in.height();

    uploadSource(in);
    ensureFbo(m_fboA, QSize(w, h));
    ensureFbo(m_fboB, QSize(w, h));
    if (!m_fboA->isValid() || !m_fboB->isValid())
        return fail(QStringLiteral("framebuffer incomplete"));

    m_box.bind();
    m_box.setUniformValue("uSrc", 0);
    m_box.setUniformValue("uW", w);
    m_box.setUniformValue("uH", h);
    m_f->glActiveTexture(GL_TEXTURE0);
    m_f->glViewport(0, 0, w, h);

    // Six passes: three box iterations, each horizontal then vertical, ping-ponging
    // A -> B -> A ... The first pass reads the uploaded texture; every later pass
    // reads the FBO the previous one wrote. Ends in m_fboB.
    GLuint readTex = m_tex;
    for (const int r : boxRadii(radius)) {
        m_box.setUniformValue("uRadius", r);
        for (const int horizontal : {1, 0}) {
            auto& target = (horizontal == 1) ? m_fboA : m_fboB;
            target->bind();
            m_f->glBindTexture(GL_TEXTURE_2D, readTex);
            m_box.setUniformValue("uHorizontal", horizontal);
            drawFullscreen();
            readTex = target->texture();
        }
    }

    if (m_f->glGetError() != GL_NO_ERROR)
        return fail(QStringLiteral("GL error during blur"));

    // toImage(false): GL's origin is bottom-left and Qt's is top-left, but the
    // upload flipped it once already, so the two cancel and no flip is wanted.
    QImage out = m_fboB->toImage(false);
    if (out.isNull())
        return fail(QStringLiteral("framebuffer readback failed"));

    // toImage() labels the result premultiplied. Our pixels never were: we uploaded
    // straight ARGB32 bytes and the shader only averages them, so the bytes are
    // already correct non-premultiplied ARGB32. Reinterpret the tag rather than
    // convertToFormat(), which would "un-premultiply" and corrupt any pixel with
    // alpha < 255.
    out.reinterpretAsFormat(QImage::Format_ARGB32);
    return out;
}

GpuBlurContext& context()
{
    static GpuBlurContext ctx;
    return ctx;
}

} // namespace

bool isSoftwareRenderer(const QString& glRenderer)
{
    const QString r = glRenderer.toLower();
    return r.contains(QLatin1String("llvmpipe"))
        || r.contains(QLatin1String("softpipe"))
        || r.contains(QLatin1String("swrast"))
        || r.contains(QLatin1String("gdi generic"))
        || r.contains(QLatin1String("basic render"))
        || r.contains(QLatin1String("software adapter"));
}

QImage gpuBoxBlur(const QImage& src, int radius)
{
    return context().blur(src, radius);
}

QString gpuBlurStatus()
{
    return context().status();
}

} // namespace render
