#pragma once

#include <QObject>
#include <QHash>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QString>
#include <memory>
#include <vector>

#include "core/annotation/Annotation.h"

class QUndoStack;
class QPainter;
class QJsonObject;

// The ordered set of annotations on a snip (or, later, a re-edited pin), with an
// undo/redo command stack, hit-testing, deterministic flatten onto the captured
// image, and JSON (de)serialization for pin re-edit + session restore. Pure
// logic (Qt Gui only, no widgets/Win32) → headlessly unit-tested. Stage 4.
class AnnotationDocument : public QObject {
    Q_OBJECT
public:
    explicit AnnotationDocument(QObject* parent = nullptr);
    ~AnnotationDocument() override;

    // Undoable mutations. add() assigns & returns the new id (0 if null/empty,
    // i.e. a degenerate shape that should not be recorded).
    int add(std::unique_ptr<annotation::Annotation> a);
    void remove(int id);
    void move(int id, const QPoint& delta);
    // Record an ALREADY-APPLIED drag as one undo step. A drag renders by translating the
    // annotation directly on every mouse-move; calling move() per event would push one
    // command per event, so a single drag would take dozens of Ctrl+Z to undo. The caller
    // instead moves live and calls this once on release with the gesture's total delta:
    // it rewinds the live movement and re-applies it through the undo stack.
    void moveApplied(int id, const QPoint& totalDelta);
    // The same for an ALREADY-APPLIED size change (number-label wheel resize): the UI
    // scales `style.fontPx` live per wheel event and calls this once the gesture ends,
    // passing the size it started at. Unlike a drag a wheel burst has no release event,
    // so the caller flushes on idle. No-op when the size did not actually change.
    void resizeApplied(int id, qreal beforeFontPx);
    // Record an ALREADY-APPLIED resize/rotate (drag of a shape's grip or spin handle) as
    // one undo step. Like moveApplied: the UI reshapes the annotation live per mouse-move,
    // then calls this once on release with the geometry+rotation it started at. No-op when
    // nothing changed.
    void reshapeApplied(int id, const QVector<QPoint>& beforeGeom, qreal beforeRot);
    // Restyle a shape (paint-bar recolor / width / fill / redact-strength on the selected
    // shape) as one undo step. Captures the current style as "before" and applies `after`.
    void restyle(int id, const annotation::Style& after);
    // Record an ALREADY-APPLIED restyle (a wheel width/strength burst, applied live per
    // notch) as one undo step — like resizeApplied. No-op when the style is unchanged.
    void restyleApplied(int id, const annotation::Style& before);
    // Set several annotations' text in ONE undo step. Used by the number-label renumber,
    // where changing a badge's number also swaps the number of whichever badge held it —
    // two annotations change, and a single Ctrl+Z must revert both. `before`/`after` are
    // id → text; ids missing from the document are skipped. Applying is idempotent, so
    // the caller may mutate live for display and push this once when the edit is done.
    void setTexts(const QHash<int, QString>& before, const QHash<int, QString>& after);
    void clearAll();

    // Group several mutations into ONE undo step. Deleting a number label also renumbers
    // the badges above it to close the gap: that is a remove + a setTexts, and one Ctrl+Z
    // must bring back both the badge and the old numbering. Always pair the calls.
    void beginMacro();
    void endMacro();

    // Hard reset: drop every annotation AND the undo history, so nothing can be undone
    // back into existence. Unlike clearAll() this is not itself undoable — it is for
    // when the annotations' subject is gone (e.g. PinModel::setSource replacing the
    // pixels the geometry referred to), not for a user-facing "clear".
    void reset();

    int count() const { return int(m_items.size()); }
    bool isEmpty() const { return m_items.empty(); }
    const std::vector<std::unique_ptr<annotation::Annotation>>& items() const { return m_items; }
    annotation::Annotation* hitTest(const QPoint& globalLogical) const;  // topmost or nullptr

    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;

    // Paint every annotation in z-order. The painter must already be transformed so
    // that global-logical coordinates land correctly; `ctx` lets redaction tools
    // sample the source pixels. `draft` (optional) is a live, not-yet-committed
    // annotation painted last (on top) — passing it here (rather than painting it
    // separately) lets a draft mosaic/blur sample the committed annotations beneath
    // it through the same composite pass.
    void paint(QPainter& p, const annotation::PaintCtx& ctx,
               const annotation::Annotation* draft = nullptr) const;

    // Bake annotations onto a copy of the composited selection. `base` is the
    // selection at device resolution; `selectionLogical` is its global-logical rect.
    QImage flatten(const QImage& base, const QRect& selectionLogical) const;

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& o);

signals:
    void changed();

private:
    // Command-facing primitives + change signal (commands are friends).
    friend class AddCmd;
    friend class RemoveCmd;
    friend class MoveCmd;
    friend class FontSizeCmd;
    friend class ReshapeCmd;
    friend class RestyleCmd;
    friend class SetTextsCmd;
    friend class ClearCmd;
    void clearContents();                                                 // items + undo, no signal
    void insertAt(std::unique_ptr<annotation::Annotation> a, int index);  // index<0 → append
    std::unique_ptr<annotation::Annotation> takeById(int id, int* outIndex);
    int indexOf(int id) const;
    void translateById(int id, const QPoint& d);
    void applyTexts(const QHash<int, QString>& texts);   // set text on each listed id
    void setFontSizeById(int id, qreal px);
    void reshapeById(int id, const QVector<QPoint>& geom, qreal rot);   // set geometry + rotation
    void setStyleById(int id, const annotation::Style& s);
    void emitChanged() { emit changed(); }

    std::vector<std::unique_ptr<annotation::Annotation>> m_items;  // back() = topmost
    QUndoStack* m_undo = nullptr;
    int m_nextId = 1;
};
