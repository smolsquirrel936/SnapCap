#include "core/PinModel.h"

#include <QTransform>
#include <QtMath>
#include <QJsonObject>

#include "core/annotation/AnnotationDocument.h"

namespace {
constexpr qreal kMinScale = 0.05;
constexpr qreal kMaxScale = 20.0;
constexpr qreal kMinOpacity = 0.1;
constexpr qreal kMaxOpacity = 1.0;
}

PinModel::PinModel(const QImage& source, const QString& sourceText)
    : m_source(source.convertToFormat(QImage::Format_ARGB32)), m_sourceText(sourceText),
      m_annotations(std::make_unique<AnnotationDocument>())
{
}

PinModel::~PinModel() = default;

void PinModel::rotateCW()  { m_rotation = (m_rotation + 90) % 360; }
void PinModel::rotateCCW() { m_rotation = (m_rotation + 270) % 360; }
void PinModel::flipHorizontal() { m_flipH = !m_flipH; }
void PinModel::flipVertical()   { m_flipV = !m_flipV; }
void PinModel::toggleGrayscale() { m_grayscale = !m_grayscale; }
void PinModel::toggleInvert()    { m_inverted = !m_inverted; }

void PinModel::setScale(qreal s)
{
    qreal hi = kMaxScale;
    if (m_maxLongSide > 0) {
        const int longSide = qMax(m_source.width(), m_source.height());
        if (longSide > 0)                         // never below kMinScale even for a tiny cap
            hi = qMin(hi, qMax(kMinScale, qreal(m_maxLongSide) / longSide));
    }
    m_scale = qBound(kMinScale, s, hi);
}
void PinModel::scaleBy(qreal f)    { if (f > 0.0) setScale(m_scale * f); }
void PinModel::setMaxLongSide(int px) { m_maxLongSide = qMax(0, px); setScale(m_scale); }
void PinModel::setOpacity(qreal o) { m_opacity = qBound(kMinOpacity, o, kMaxOpacity); }
void PinModel::adjustOpacity(qreal d) { setOpacity(m_opacity + d); }

void PinModel::reset()
{
    m_rotation = 0;
    m_flipH = m_flipV = false;
    m_grayscale = m_inverted = false;
    m_scale = 1.0;
    m_opacity = 1.0;
}

void PinModel::setSource(const QImage& source, const QString& sourceText)
{
    m_source = source.convertToFormat(QImage::Format_ARGB32);
    m_frames.clear();                // new pixels → the old animation is not what this pin shows
    m_delaysMs.clear();
    m_frame = 0;
    m_sourceText = sourceText;
    m_sourcePath.clear();            // new pixels → the old file is no longer what this pin shows
    m_annotations->reset();          // ...and the old annotations' geometry is meaningless on them
    m_sourceDpr = 0.0;               // a replacement (Ctrl+V) is clipboard-sourced → display dpr
    reset();
}

void PinModel::setAnimation(const QVector<QImage>& frames, const QVector<int>& delaysMs)
{
    if (frames.size() < 2 || frames.size() != delaysMs.size())
        return;                      // a single frame is just a still — leave the source alone
    m_frames.clear();
    m_frames.reserve(frames.size());
    for (const QImage& f : frames)   // the source format the rest of the class assumes
        m_frames.append(f.convertToFormat(QImage::Format_ARGB32));
    m_delaysMs = delaysMs;
    m_frame = 0;
    m_source = m_frames.first();
}

int PinModel::currentDelayMs() const
{
    return isAnimated() ? m_delaysMs.at(m_frame) : 0;
}

void PinModel::advanceFrame()
{
    if (!isAnimated())
        return;
    m_frame = (m_frame + 1) % int(m_frames.size());
    m_source = m_frames.at(m_frame);   // pixels only: transforms/annotations/path all persist
}

QImage PinModel::processedSource() const
{
    if (m_source.isNull() || (!m_grayscale && !m_inverted))
        return m_source;

    QImage img = m_source;                  // implicitly shared; the writes below detach it
    if (m_grayscale) {
        // Not Format_Grayscale8: that drops the alpha channel, which would turn a
        // transparent pin opaque. m_source is always ARGB32 (non-premultiplied), so the
        // RGB here is straight and qGray's luminance weighting applies directly.
        for (int y = 0; y < img.height(); ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                const int g = qGray(line[x]);
                line[x] = qRgba(g, g, g, qAlpha(line[x]));
            }
        }
    }
    if (m_inverted)
        img.invertPixels(QImage::InvertRgb);   // InvertRgb, not InvertRgba: keep alpha
    return img;
}

QImage PinModel::annotatedSource() const
{
    // Annotations are part of the image, so they bake onto the source pixels before any
    // transform. Their geometry IS source px, so the "selection" handed to flatten() is
    // simply the whole source rect — an identity map.
    const QImage base = processedSource();
    if (base.isNull() || !m_annotations || m_annotations->isEmpty())
        return base;
    return m_annotations->flatten(base, QRect(QPoint(0, 0), base.size()));
}

QImage PinModel::compose() const
{
    if (m_source.isNull())
        return m_source;

    QImage img = annotatedSource();
    if (m_rotation != 0)                                   // 90° multiples are lossless
        img = img.transformed(QTransform().rotate(m_rotation), Qt::FastTransformation);
    if (m_flipH || m_flipV)
        img = img.mirrored(m_flipH, m_flipV);
    if (!qFuzzyCompare(m_scale, 1.0)) {
        const QSize s(qMax(1, int(qRound(img.width() * m_scale))),
                      qMax(1, int(qRound(img.height() * m_scale))));
        img = img.scaled(s, Qt::IgnoreAspectRatio,
                         m_smoothScaling ? Qt::SmoothTransformation : Qt::FastTransformation);
    }
    return img;
}

QTransform PinModel::sourceToComposed() const
{
    // The twin of compose()'s rotate → flip → scale. QTransform applies the LAST call
    // first, so the steps are written here in reverse order: scale first (applied last),
    // rotation last (applied first).
    const qreal w = m_source.width(), h = m_source.height();
    const bool swap = (m_rotation == 90 || m_rotation == 270);
    const qreal rw = swap ? h : w;      // size after rotation, before flip/scale
    const qreal rh = swap ? w : h;

    QTransform t;
    t.scale(m_scale, m_scale);
    // Flips live in the ROTATED image's box: mirroring maps [0,rw] onto itself.
    if (m_flipH) { t.translate(rw, 0); t.scale(-1, 1); }
    if (m_flipV) { t.translate(0, rh); t.scale(1, -1); }
    // QImage::transformed() shifts the rotated result back to the origin; these
    // translations reproduce that shift, so the output starts at (0,0) like compose().
    switch (m_rotation) {
        case 90:  t.translate(h, 0); t.rotate(90);  break;   // (x,y) → (h - y, x)
        case 180: t.translate(w, h); t.rotate(180); break;   // (x,y) → (w - x, h - y)
        case 270: t.translate(0, w); t.rotate(270); break;   // (x,y) → (y, w - x)
        default: break;
    }
    return t;
}

QJsonObject PinModel::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("rotation")] = m_rotation;
    o[QStringLiteral("flipH")]    = m_flipH;
    o[QStringLiteral("flipV")]    = m_flipV;
    o[QStringLiteral("grayscale")] = m_grayscale;
    o[QStringLiteral("inverted")]  = m_inverted;
    o[QStringLiteral("scale")]    = m_scale;
    o[QStringLiteral("opacity")]  = m_opacity;
    o[QStringLiteral("group")]    = m_group;
    // Persist the intrinsic capture dpr: it fixes the pin's logical size (source-px / dpr) on
    // every monitor. PNG carries no dpr, so without this a restored/group-switched snip falls
    // back to the display dpr and resizes. 0 = unset (clipboard/restored images).
    if (m_sourceDpr > 0.0)
        o[QStringLiteral("sourceDpr")] = m_sourceDpr;
    if (!m_sourceText.isEmpty())
        o[QStringLiteral("text")] = m_sourceText;
    if (!m_sourcePath.isEmpty())
        o[QStringLiteral("sourcePath")] = m_sourcePath;
    // Only a per-pin override is persisted; a pin that never touched its Zoom ▸ Smoothing
    // item stays on whatever paste/behavior/smoothScaling says at restore time.
    if (m_smoothOverridden)
        o[QStringLiteral("smooth")] = m_smoothScaling;
    return o;
}

void PinModel::fromJson(const QJsonObject& o)
{
    // Normalize rotation to a 0/90/180/270 multiple; reuse the setters' clamping.
    m_rotation = ((o.value(QStringLiteral("rotation")).toInt(0) % 360) + 360) % 360;
    m_rotation -= m_rotation % 90;
    m_flipH = o.value(QStringLiteral("flipH")).toBool(false);
    m_flipV = o.value(QStringLiteral("flipV")).toBool(false);
    m_grayscale = o.value(QStringLiteral("grayscale")).toBool(false);
    m_inverted  = o.value(QStringLiteral("inverted")).toBool(false);
    setScale(o.value(QStringLiteral("scale")).toDouble(1.0));
    setOpacity(o.value(QStringLiteral("opacity")).toDouble(1.0));
    m_group = o.value(QStringLiteral("group")).toInt(0);
    setSourceDpr(o.value(QStringLiteral("sourceDpr")).toDouble(0.0));   // clamps <=0 → 0 (unset)
    if (o.contains(QStringLiteral("text")))
        m_sourceText = o.value(QStringLiteral("text")).toString();
    if (o.contains(QStringLiteral("sourcePath")))
        m_sourcePath = o.value(QStringLiteral("sourcePath")).toString();
    if (o.contains(QStringLiteral("smooth")))
        overrideSmoothScaling(o.value(QStringLiteral("smooth")).toBool(true));
}

QSize PinModel::composedSize() const
{
    QSize s = m_source.size();
    if (m_rotation == 90 || m_rotation == 270)
        s.transpose();
    return QSize(qMax(1, int(qRound(s.width() * m_scale))),
                 qMax(1, int(qRound(s.height() * m_scale))));
}
