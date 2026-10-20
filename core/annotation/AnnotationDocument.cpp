#include "core/annotation/AnnotationDocument.h"

#include <QUndoStack>
#include <QUndoCommand>
#include <QPainter>
#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>

using annotation::Annotation;

// --- undo commands ---------------------------------------------------------
// Each transfers ownership of the affected annotation(s) in/out of the document
// as it is undone/redone, so an undone Add (or a Clear) still keeps its objects
// alive on the stack until the stack itself is trimmed.

class AddCmd : public QUndoCommand {
public:
    AddCmd(AnnotationDocument* d, std::unique_ptr<Annotation> a)
        : m_doc(d), m_held(std::move(a)) { m_id = m_held ? m_held->id : 0; }
    void redo() override { m_doc->insertAt(std::move(m_held), -1); m_doc->emitChanged(); }
    void undo() override { int i = 0; m_held = m_doc->takeById(m_id, &i); m_doc->emitChanged(); }
private:
    AnnotationDocument* m_doc;
    std::unique_ptr<Annotation> m_held;
    int m_id;
};

class RemoveCmd : public QUndoCommand {
public:
    RemoveCmd(AnnotationDocument* d, int id) : m_doc(d), m_id(id) {}
    void redo() override { m_held = m_doc->takeById(m_id, &m_index); m_doc->emitChanged(); }
    void undo() override { m_doc->insertAt(std::move(m_held), m_index); m_doc->emitChanged(); }
private:
    AnnotationDocument* m_doc;
    int m_id;
    int m_index = -1;
    std::unique_ptr<Annotation> m_held;
};

class MoveCmd : public QUndoCommand {
public:
    MoveCmd(AnnotationDocument* d, int id, const QPoint& delta)
        : m_doc(d), m_id(id), m_delta(delta) {}
    void redo() override { m_doc->translateById(m_id, m_delta);  m_doc->emitChanged(); }
    void undo() override { m_doc->translateById(m_id, -m_delta); m_doc->emitChanged(); }
private:
    AnnotationDocument* m_doc;
    int m_id;
    QPoint m_delta;
};

// A size change (number-label wheel resize) as one step, whatever the wheel-event count.
class FontSizeCmd : public QUndoCommand {
public:
    FontSizeCmd(AnnotationDocument* d, int id, qreal before, qreal after)
        : m_doc(d), m_id(id), m_before(before), m_after(after) {}
    void redo() override { m_doc->setFontSizeById(m_id, m_after);  m_doc->emitChanged(); }
    void undo() override { m_doc->setFontSizeById(m_id, m_before); m_doc->emitChanged(); }
private:
    AnnotationDocument* m_doc;
    int m_id;
    qreal m_before, m_after;
};

// A resize/rotate as one step: before/after snapshots of geometry + rotation.
class ReshapeCmd : public QUndoCommand {
public:
    ReshapeCmd(AnnotationDocument* d, int id,
               QVector<QPoint> beforeGeom, qreal beforeRot,
               QVector<QPoint> afterGeom, qreal afterRot)
        : m_doc(d), m_id(id),
          m_beforeGeom(std::move(beforeGeom)), m_afterGeom(std::move(afterGeom)),
          m_beforeRot(beforeRot), m_afterRot(afterRot) {}
    void redo() override { m_doc->reshapeById(m_id, m_afterGeom, m_afterRot);   m_doc->emitChanged(); }
    void undo() override { m_doc->reshapeById(m_id, m_beforeGeom, m_beforeRot); m_doc->emitChanged(); }
private:
    AnnotationDocument* m_doc;
    int m_id;
    QVector<QPoint> m_beforeGeom, m_afterGeom;
    qreal m_beforeRot, m_afterRot;
};

// A restyle (paint-bar recolor / width / fill / redact on a selected shape) as one step.
class RestyleCmd : public QUndoCommand {
public:
    RestyleCmd(AnnotationDocument* d, int id,
               const annotation::Style& before, const annotation::Style& after)
        : m_doc(d), m_id(id), m_before(before), m_after(after) {}
    void redo() override { m_doc->setStyleById(m_id, m_after);  m_doc->emitChanged(); }
    void undo() override { m_doc->setStyleById(m_id, m_before); m_doc->emitChanged(); }
private:
    AnnotationDocument* m_doc;
    int m_id;
    annotation::Style m_before, m_after;
};

// Multi-annotation text change as one step (number-label renumber: the edited badge and
// the badge it swapped with). Both directions just write a snapshot, so pushing this
// after the caller already mutated for display is a no-op redo.
class SetTextsCmd : public QUndoCommand {
public:
    SetTextsCmd(AnnotationDocument* d, QHash<int, QString> before, QHash<int, QString> after)
        : m_doc(d), m_before(std::move(before)), m_after(std::move(after)) {}
    void redo() override { m_doc->applyTexts(m_after);  m_doc->emitChanged(); }
    void undo() override { m_doc->applyTexts(m_before); m_doc->emitChanged(); }
private:
    AnnotationDocument* m_doc;
    QHash<int, QString> m_before, m_after;
};

class ClearCmd : public QUndoCommand {
public:
    explicit ClearCmd(AnnotationDocument* d) : m_doc(d) {}
    void redo() override { m_held = std::move(m_doc->m_items); m_doc->m_items.clear(); m_doc->emitChanged(); }
    void undo() override { m_doc->m_items = std::move(m_held); m_doc->emitChanged(); }
private:
    AnnotationDocument* m_doc;
    std::vector<std::unique_ptr<Annotation>> m_held;
};

// --- document --------------------------------------------------------------

AnnotationDocument::AnnotationDocument(QObject* parent)
    : QObject(parent), m_undo(new QUndoStack(this))
{
}

AnnotationDocument::~AnnotationDocument() = default;

int AnnotationDocument::add(std::unique_ptr<Annotation> a)
{
    if (!a || a->isEmpty())
        return 0;
    if (a->id == 0)
        a->id = m_nextId++;
    const int id = a->id;
    m_undo->push(new AddCmd(this, std::move(a)));
    return id;
}

void AnnotationDocument::remove(int id)
{
    if (indexOf(id) >= 0)
        m_undo->push(new RemoveCmd(this, id));
}

void AnnotationDocument::move(int id, const QPoint& delta)
{
    if (delta.isNull() || indexOf(id) < 0)
        return;
    m_undo->push(new MoveCmd(this, id, delta));
}

void AnnotationDocument::moveApplied(int id, const QPoint& total)
{
    if (total.isNull() || indexOf(id) < 0)
        return;
    translateById(id, -total);                    // rewind the live (uncommanded) drag
    m_undo->push(new MoveCmd(this, id, total));   // ...and re-apply it as one step
}

void AnnotationDocument::resizeApplied(int id, qreal beforePx)
{
    const int i = indexOf(id);
    if (i < 0)
        return;
    const qreal after = m_items[size_t(i)]->style.fontPx;
    if (qFuzzyCompare(after, beforePx))
        return;
    m_items[size_t(i)]->style.fontPx = beforePx;          // rewind the live resize
    m_undo->push(new FontSizeCmd(this, id, beforePx, after));   // ...re-apply as one step
}

void AnnotationDocument::setFontSizeById(int id, qreal px)
{
    const int i = indexOf(id);
    if (i >= 0)
        m_items[size_t(i)]->style.fontPx = px;
}

void AnnotationDocument::reshapeApplied(int id, const QVector<QPoint>& beforeGeom, qreal beforeRot)
{
    const int i = indexOf(id);
    if (i < 0)
        return;
    Annotation* a = m_items[size_t(i)].get();
    const QVector<QPoint> afterGeom = a->geometry();
    const qreal afterRot = a->rotation();
    if (afterGeom == beforeGeom && qFuzzyCompare(afterRot + 1.0, beforeRot + 1.0))
        return;                                       // no net change
    a->setGeometry(beforeGeom);                       // rewind the live edit...
    a->setRotation(beforeRot);
    m_undo->push(new ReshapeCmd(this, id, beforeGeom, beforeRot, afterGeom, afterRot));  // ...re-apply as one step
}

void AnnotationDocument::reshapeById(int id, const QVector<QPoint>& geom, qreal rot)
{
    const int i = indexOf(id);
    if (i >= 0) {
        m_items[size_t(i)]->setGeometry(geom);
        m_items[size_t(i)]->setRotation(rot);
    }
}

void AnnotationDocument::restyle(int id, const annotation::Style& after)
{
    const int i = indexOf(id);
    if (i < 0)
        return;
    m_undo->push(new RestyleCmd(this, id, m_items[size_t(i)]->style, after));
}

void AnnotationDocument::restyleApplied(int id, const annotation::Style& before)
{
    const int i = indexOf(id);
    if (i < 0)
        return;
    const annotation::Style after = m_items[size_t(i)]->style;
    if (after == before)
        return;                                       // burst netted no change
    m_items[size_t(i)]->style = before;               // rewind the live change...
    m_undo->push(new RestyleCmd(this, id, before, after));   // ...re-apply as one step
}

void AnnotationDocument::setStyleById(int id, const annotation::Style& s)
{
    const int i = indexOf(id);
    if (i >= 0)
        m_items[size_t(i)]->style = s;
}

void AnnotationDocument::setTexts(const QHash<int, QString>& before,
                                  const QHash<int, QString>& after)
{
    if (before == after)
        return;
    m_undo->push(new SetTextsCmd(this, before, after));
}

void AnnotationDocument::applyTexts(const QHash<int, QString>& texts)
{
    for (auto it = texts.cbegin(); it != texts.cend(); ++it) {
        const int i = indexOf(it.key());
        if (i >= 0)
            m_items[size_t(i)]->setText(it.value());
    }
}

void AnnotationDocument::clearAll()
{
    if (!m_items.empty())
        m_undo->push(new ClearCmd(this));
}

void AnnotationDocument::beginMacro()
{
    m_undo->beginMacro(QStringLiteral("edit"));   // label is internal; never shown in UI
}

void AnnotationDocument::endMacro()
{
    m_undo->endMacro();
}

void AnnotationDocument::clearContents()
{
    m_undo->clear();
    m_items.clear();
    m_nextId = 1;
}

void AnnotationDocument::reset()
{
    clearContents();
    emit changed();
}

annotation::Annotation* AnnotationDocument::hitTest(const QPoint& g) const
{
    for (auto it = m_items.rbegin(); it != m_items.rend(); ++it)
        if ((*it)->hitTest(g))
            return it->get();
    return nullptr;
}

void AnnotationDocument::undo() { m_undo->undo(); }
void AnnotationDocument::redo() { m_undo->redo(); }
bool AnnotationDocument::canUndo() const { return m_undo->canUndo(); }
bool AnnotationDocument::canRedo() const { return m_undo->canRedo(); }

void AnnotationDocument::paint(QPainter& p, const annotation::PaintCtx& ctx,
                               const annotation::Annotation* draft) const
{
    using annotation::isRedactTool;
    const bool draftRedacts = draft && isRedactTool(draft->tool());
    const bool anyRedact = draftRedacts
        || std::any_of(m_items.begin(), m_items.end(),
                       [](const auto& a) { return isRedactTool(a->tool()); });

    // Common, cheap path: nothing redacts (or we have no pixels to sample), so draw the
    // stack straight onto the caller's painter.
    if (!anyRedact || !ctx.base) {
        for (const auto& a : m_items) {
            p.save();
            a->paint(p, ctx);
            p.restore();
        }
        if (draft) {
            p.save();
            draft->paint(p, ctx);
            p.restore();
        }
        return;
    }

    // Redaction must reflect the annotations *beneath* it, not just the clean capture.
    // Build the annotation layer on an offscreen copy of the base, in the same pixel
    // coordinates, and let each redaction sample that evolving composite (everything
    // painted so far). Eraser tools still restore the clean original via ctx.base.
    QImage composite = ctx.base->copy();
    {
        QPainter cp(&composite);
        cp.setRenderHints(p.renderHints());
        // Map global-logical coordinates into composite pixels: px = (L - origin) * scale.
        cp.scale(ctx.scale, ctx.scale);
        cp.translate(-ctx.originLogical);

        annotation::PaintCtx cctx = ctx;
        cctx.redactSource = &composite;   // redaction reads the composite-so-far

        for (const auto& a : m_items) {
            cp.save();
            a->paint(cp, cctx);
            cp.restore();
        }
        if (draft) {
            cp.save();
            draft->paint(cp, cctx);
            cp.restore();
        }
    }

    // Blit the finished layer back onto the caller's painter (clipped by p to the
    // visible / selection region) in global-logical space.
    const QRectF dst(ctx.originLogical,
                     QSizeF(composite.width() / ctx.scale, composite.height() / ctx.scale));
    p.drawImage(dst, composite);
}

QImage AnnotationDocument::flatten(const QImage& base, const QRect& sel) const
{
    if (base.isNull())
        return base;
    QImage out = base.convertToFormat(QImage::Format_ARGB32);
    if (m_items.empty() || sel.width() <= 0 || sel.height() <= 0)
        return out;

    // sx/sy below map `sel` onto out's DEVICE pixels — but a QPainter on an image that
    // carries a devicePixelRatio works in LOGICAL px, and would quietly add another
    // x-dpr about the origin, scaling every item away from the top-left. A pin's source
    // keeps the capture dpr (Application passes img.devicePixelRatio() to addImage), so
    // its baked annotations landed bottom-right of the live draft on any scaled display.
    // Paint in device px, then hand the dpr back to the caller untouched.
    const qreal dpr = out.devicePixelRatio();
    out.setDevicePixelRatio(1.0);

    QPainter p(&out);
    const qreal sx = qreal(out.width()) / sel.width();
    const qreal sy = qreal(out.height()) / sel.height();
    p.scale(sx, sy);
    p.translate(-sel.left(), -sel.top());

    annotation::PaintCtx ctx;
    ctx.base = &base;                  // sample clean original pixels for redaction
    ctx.originLogical = sel.topLeft();
    ctx.scale = sx;
    paint(p, ctx);
    p.end();
    out.setDevicePixelRatio(dpr);
    return out;
}

QJsonObject AnnotationDocument::toJson() const
{
    QJsonArray arr;
    for (const auto& a : m_items)
        arr.append(a->toJson());
    QJsonObject o;
    o[QStringLiteral("items")] = arr;
    return o;
}

void AnnotationDocument::fromJson(const QJsonObject& o)
{
    clearContents();
    const QJsonArray arr = o.value(QStringLiteral("items")).toArray();
    for (const QJsonValue& v : arr) {
        std::unique_ptr<Annotation> a = Annotation::fromJson(v.toObject());
        if (!a) continue;
        m_nextId = qMax(m_nextId, a->id + 1);
        m_items.push_back(std::move(a));
    }
    emit changed();
}

// --- command-facing primitives ---------------------------------------------

void AnnotationDocument::insertAt(std::unique_ptr<Annotation> a, int index)
{
    if (!a)
        return;
    if (index < 0 || index > int(m_items.size()))
        m_items.push_back(std::move(a));
    else
        m_items.insert(m_items.begin() + index, std::move(a));
}

std::unique_ptr<Annotation> AnnotationDocument::takeById(int id, int* outIndex)
{
    const int idx = indexOf(id);
    if (outIndex) *outIndex = idx;
    if (idx < 0)
        return nullptr;
    std::unique_ptr<Annotation> a = std::move(m_items[idx]);
    m_items.erase(m_items.begin() + idx);
    return a;
}

int AnnotationDocument::indexOf(int id) const
{
    for (int i = 0; i < int(m_items.size()); ++i)
        if (m_items[i]->id == id)
            return i;
    return -1;
}

void AnnotationDocument::translateById(int id, const QPoint& d)
{
    const int idx = indexOf(id);
    if (idx >= 0)
        m_items[idx]->translate(d);
}
