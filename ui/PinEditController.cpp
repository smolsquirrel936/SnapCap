#include "ui/PinEditController.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QStyleHints>
#include <QThread>
#include <QTimer>
#include <QTransform>
#include <QtMath>

#include <cmath>

#include "core/PinModel.h"
#include "core/Settings.h"
#include "core/annotation/AnnotationDocument.h"
#include "core/annotation/ShapeConstraints.h"
#include "platform/platform.h"
#include "ui/OcrBar.h"
#include "ui/PaintBar.h"
#include "ui/PinWindow.h"
#include "ui/TextBoxEditor.h"

namespace {
constexpr int kGap = 8;        // pin ↔ toolbar
constexpr int kRowGap = 4;     // main bar ↔ sub-bar
constexpr int kDefTextW = 64;  // starting text box (grows right as you type, down on Enter)
constexpr int kDefTextH = 44;
constexpr double kLineDragSlop = 4.0;   // press must move this far to count as a drag
}

PinEditController::PinEditController(PinWindow* pin, Settings* settings)
    : QObject(pin), m_pin(pin), m_model(pin->model()), m_settings(settings)
{
    m_ocr = PinWindow::ocr();                   // app-wide OCR backend (may be null)
    m_bar = new PaintBar();
    m_bar->setHost(PaintBar::Host::Pin);       // hides pin/save/copy; cancel = close toolbar
    m_bar->setSettings(m_settings);
    m_subBar = m_bar->subBar();

    // Both bars float as their own top-level windows: the pin is frameless, so a child
    // widget could not extend past its edge. WA_ShowWithoutActivating +
    // WindowDoesNotAcceptFocus keep the pin looking (and behaving) focused while the
    // toolbar is clicked.
    const Qt::WindowFlags flags = Qt::Tool | Qt::FramelessWindowHint
                                | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus;
    m_bar->setParent(nullptr);
    m_bar->setWindowFlags(flags);
    m_bar->setAttribute(Qt::WA_ShowWithoutActivating);
    if (m_subBar) {
        m_subBar->setParent(nullptr);          // out of the bar → ours to delete
        m_subBar->setWindowFlags(flags);
        m_subBar->setAttribute(Qt::WA_ShowWithoutActivating);
    }

    // Seed the style from the bar's current state (it remembers per-tool widths/colours).
    m_style.color = m_bar->currentColor();
    m_style.penWidth = m_bar->penWidth();
    m_style.redactStrength = m_bar->redactStrength();
    m_style.filled = m_bar->currentFilled();
    m_style.startCap = m_bar->currentStartCap();
    m_style.endCap = m_bar->currentEndCap();
    m_style.curved = m_bar->currentCurved();
    m_style.lineStyle = m_bar->currentLineStyle();
    m_style.bold = m_bar->currentBold();
    m_style.italic = m_bar->currentItalic();
    m_style.hollowText = m_bar->currentHollowText();
    m_style.fontFamily = m_bar->currentFontFamily();
    m_style.fontPx = m_bar->currentFontSize();
    m_style.label = m_bar->currentLabelStyle();
    m_style.numberStyle = m_bar->currentNumberStyle();

    wireBar();
    syncUndoRedo();
    reposition();
}

PinEditController::~PinEditController()
{
    // No commit here: the destructor also runs from ~PinWindow, where touching the pin
    // (refresh/setFocus) is unsafe. PinWindow::closeToolbar() calls finishEditing() first
    // on the user-facing path, so only a pin that is itself going away discards a box.
    delete m_textEditor;
    delete m_ocrBar;        // own top-level window (like m_subBar) → delete it ourselves
    delete m_subBar;        // reparented out of m_bar, so it is not deleted with it
    delete m_bar;
}

void PinEditController::finishEditing()
{
    commitTextEditor();
}

void PinEditController::wireBar()
{
    connect(m_bar, &PaintBar::toolSelected, this, [this](annotation::ToolId t) {
        setTool(t);
        reposition();                  // a tool's sub-panel (un)shows → resize + re-anchor
    });
    connect(m_bar, &PaintBar::colorSelected, this, [this](const QColor& c) {
        m_style.color = c;
        if (m_textEditor) m_textEditor->setTextColor(c);
    });
    connect(m_bar, &PaintBar::penWidthChanged, this, [this](int w) { m_style.penWidth = w; });
    connect(m_bar, &PaintBar::fillSelected, this, [this](bool f) {
        m_style.filled = f;
        reposition();                  // the dash dropdown (un)shows → resize + re-anchor
    });
    connect(m_bar, &PaintBar::lineStyleChanged, this, [this](annotation::LineStyle s) {
        m_style.lineStyle = s;
        if (m_draft) m_draft->style.lineStyle = s;  // live-update an in-progress stroke
        m_pin->update();
    });
    connect(m_bar, &PaintBar::startCapChanged, this, [this](annotation::CapStyle c) {
        m_style.startCap = c;
        if (m_draft) m_draft->style.startCap = c;   // live-update an in-progress line
        m_pin->update();
    });
    connect(m_bar, &PaintBar::endCapChanged, this, [this](annotation::CapStyle c) {
        m_style.endCap = c;
        if (m_draft) m_draft->style.endCap = c;
        m_pin->update();
    });
    connect(m_bar, &PaintBar::curvedChanged, this, [this](bool c) {
        m_style.curved = c;
        if (m_draft) m_draft->style.curved = c;     // live-update an in-progress line
        m_pin->update();
    });
    connect(m_bar, &PaintBar::labelStyleChanged, this, [this](annotation::LabelStyle s) {
        m_style.label = s;
        if (m_draft) m_draft->style.label = s;      // live-update a badge being renumbered
        m_pin->update();
    });
    connect(m_bar, &PaintBar::numberStyleChanged, this,
            [this](annotation::NumberStyle s) { setNumberStyle(s); });
    connect(m_bar, &PaintBar::redactStrengthChanged, this,
            [this](int s) { m_style.redactStrength = s; });
    connect(m_bar, &PaintBar::boldChanged, this, [this](bool b) {
        m_style.bold = b;
        if (m_textEditor) m_textEditor->setBold(b);
    });
    connect(m_bar, &PaintBar::italicChanged, this, [this](bool b) {
        m_style.italic = b;
        if (m_textEditor) m_textEditor->setItalic(b);
    });
    connect(m_bar, &PaintBar::hollowTextChanged, this, [this](bool h) {
        m_style.hollowText = h;
        if (m_textEditor) m_textEditor->setHollow(h);
    });
    connect(m_bar, &PaintBar::fontFamilyChanged, this, [this](const QString& f) {
        m_style.fontFamily = f;
        if (m_textEditor) m_textEditor->setFontFamily(f);
    });
    connect(m_bar, &PaintBar::fontSizeChanged, this, [this](qreal px) {
        m_style.fontPx = px;
        if (m_textEditor) m_textEditor->setFontPixelSize(px);
    });
    connect(m_bar, &PaintBar::undoRequested, this, [this] {
        commitTextEditor();
        flushLabelResize();          // bank an in-flight wheel resize so Ctrl+Z undoes IT
        const int before = labelCount();
        m_model->annotations()->undo();
        syncNextLabel(before);
        syncNumberStyleFromDoc();
        refresh();
        syncUndoRedo();
    });
    connect(m_bar, &PaintBar::redoRequested, this, [this] {
        commitTextEditor();
        flushLabelResize();
        const int before = labelCount();
        m_model->annotations()->redo();
        syncNextLabel(before);
        syncNumberStyleFromDoc();
        refresh();
        syncUndoRedo();
    });
    connect(m_bar, &PaintBar::cancelRequested, this, [this] { emit closeRequested(); });
    connect(m_bar, &PaintBar::ocrToggled, this, [this](bool on) { toggleOcr(on); });
}

void PinEditController::setTool(annotation::ToolId t)
{
    if (t != m_tool) {
        resetLineState();          // a half-built line is abandoned when the tool changes
        commitTextEditor();        // switching tools bakes the open text box
        if (m_labelEditId > 0)     // ...and banks an open badge renumber
            commitLabelEdit();
        flushLabelResize();        // ...and any in-flight wheel resize
        m_labelHoverOutline.clear();
        // (Re-)selecting the number-label tool restarts the count at 1.
        if (t == annotation::ToolId::NumberLabel)
            m_nextLabel = 1;
    }
    m_tool = t;
    m_pin->unsetCursor();          // PinWindow::mouseMoveEvent re-picks it
    m_pin->update();
}

// --- OCR mode ----------------------------------------------------------------

void PinEditController::toggleOcr(bool on)
{
    if (on) {
        if (!m_ocr) {                       // no backend built → the button is a no-op
            m_bar->setOcrChecked(false);
            m_ocrActive = false;
            return;
        }
        m_ocrActive = true;                 // the bar already deselected any drawing tool
        if (!m_ocrBar) {
            m_ocrBar = new OcrBar();
            // Its own top-level window, like the bars (the pin is frameless).
            const Qt::WindowFlags flags = Qt::Tool | Qt::FramelessWindowHint
                                        | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus;
            m_ocrBar->setWindowFlags(flags);
            m_ocrBar->setAttribute(Qt::WA_ShowWithoutActivating);
            connect(m_ocrBar, &OcrBar::selectAllRequested, this, [this] {
                m_ocrLayer.selectAll();
                m_pin->update();
            });
            connect(m_ocrBar, &OcrBar::copyRequested, this, [this] { copyOcrText(); });
            connect(m_ocrBar, &OcrBar::keepLineBreaksChanged, this, [this](bool k) {
                m_ocrLayer.setKeepLineBreaks(k);
            });
            connect(m_ocrBar, &OcrBar::languageChanged, this, [this](const QString& c) {
                m_ocrLang = c;
                startOcrRecognition();
            });
        }
        m_ocrBar->setAccent(PinWindow::themeColor());
        if (m_ocrLang.isEmpty()) {
            const QStringList have = m_ocr->availableLanguages();
            const QString pref = m_settings           // Preferences → OCR default (empty = auto)
                ? m_settings->value(QStringLiteral("ocr/language")).toString() : QString();
            m_ocrLang = (!pref.isEmpty() && have.contains(pref)) ? pref
                                                                 : OcrBar::defaultLanguage(have);
        }
        m_ocrBar->setLanguages(m_ocr->availableLanguages(), m_ocrLang);
        m_ocrLayer.setKeepLineBreaks(m_ocrBar->keepLineBreaks());
        startOcrRecognition();
    } else {
        m_ocrActive = false;
        m_ocrLayer.clear();
        if (m_ocrBar) m_ocrBar->hide();
    }
    reposition();
    m_pin->update();
}

void PinEditController::startOcrRecognition()
{
    if (!m_ocr)
        return;
    // OCR the visible pin content (source px, upright — before the pin's transform stack).
    const QImage img = m_model->annotatedSource();
    if (img.isNull())
        return;
    platform::IOcr* ocr = m_ocr;
    const QString lang = m_ocrLang;
    // Recognition is slow — run it off the UI thread, guarded so a torn-down controller
    // (pin closed while recognizing) is never touched.
    QPointer<PinEditController> self(this);
    QThread* th = QThread::create([ocr, img, lang, self] {
        const platform::OcrResult r = ocr->recognize(img, lang);
        QMetaObject::invokeMethod(qApp, [self, r] { if (self) self->onOcrReady(r); },
                                  Qt::QueuedConnection);
    });
    connect(th, &QThread::finished, th, &QObject::deleteLater);
    th->start();
}

void PinEditController::onOcrReady(const platform::OcrResult& r)
{
    if (!m_ocrActive)                       // user left OCR mode while recognizing
        return;
    m_ocrLayer.setResult(r);
    if (m_ocrBar) m_ocrLayer.setKeepLineBreaks(m_ocrBar->keepLineBreaks());
    m_pin->update();
}

void PinEditController::copyOcrText()
{
    if (!m_ocrLayer.hasSelection())         // nothing picked → copy all
        m_ocrLayer.selectAll();
    const QString text = m_ocrLayer.selectedText();
    if (text.isEmpty())
        return;
    QGuiApplication::clipboard()->setText(text);
    m_pin->update();
}

// --- coordinate helpers ------------------------------------------------------

QPoint PinEditController::toSource(const QPoint& local) const
{
    return m_pin->sourceToWidget().inverted().map(QPointF(local)).toPoint();
}

qreal PinEditController::sourcePerScreen() const
{
    // How many source px one screen-logical px spans: the length of the image of a unit
    // vector under the transform, inverted. Covers rotation, flips, zoom and dpr at once.
    const QTransform t = m_pin->sourceToWidget();
    const qreal len = std::hypot(t.m11(), t.m12());
    return len > 0.0001 ? 1.0 / len : 1.0;
}

annotation::Style PinEditController::sourceStyle() const
{
    // The bar reports sizes in on-screen logical px; annotations live in source px. Scale
    // them so a stroke drawn at width N *looks* N px wide right now — and then scales with
    // the image afterwards, like the pixels it sits on.
    annotation::Style s = m_style;
    const qreal f = sourcePerScreen();
    s.penWidth = qMax(1, int(qRound(s.penWidth * f)));
    s.fontPx = s.fontPx * f;
    s.redactStrength = qMax(1, int(qRound(s.redactStrength * f)));
    return s;
}

bool PinEditController::mirroredForPin() const
{
    // An odd number of mirrors in the pin's transform flips handedness, so glyphs baked
    // into the source would read backwards. The shape stores itself pre-mirrored to cancel
    // that out (see Annotation::setMirrored).
    return m_pin->sourceToWidget().determinant() < 0;
}

qreal PinEditController::sourceRotationFor(qreal screenDeg) const
{
    // The shape paints mirror-then-rotate in source space and compose() then applies the
    // pin's own rotate/flip. Rather than case-analysing all 16 orientations, map the
    // wanted screen direction back through the live transform — it cannot drift.
    const QTransform inv = m_pin->sourceToWidget().inverted();
    const qreal a = qDegreesToRadians(mirroredForPin() ? screenDeg + 180.0 : screenDeg);
    const QPointF o = inv.map(QPointF(0, 0));
    const QPointF d = inv.map(QPointF(qCos(a), qSin(a)));
    return qRadiansToDegrees(qAtan2(d.y() - o.y(), d.x() - o.x()));
}

// --- input -------------------------------------------------------------------

bool PinEditController::onPress(const QPoint& local, Qt::MouseButton button,
                                Qt::KeyboardModifiers mods)
{
    if (m_ocrActive) {                     // OCR: left-drag selects text; swallow the rest
        if (button == Qt::LeftButton && !m_ocrLayer.isEmpty()) {
            m_ocrLayer.beginSelect(toSource(local));
            m_pin->update();
        }
        return true;                       // consume → no pin drag/resize/menu while reading
    }
    if (button == Qt::RightButton)
        return handleLineSecondary();      // right-click advances/finalizes a line only

    if (button != Qt::LeftButton || !toolActive())
        return false;                      // no tool → the pin drags/resizes as usual

    flushLabelResize();                    // a click ends any in-flight wheel resize
    const QPoint src = toSource(local);
    if (m_tool == annotation::ToolId::Text) {
        placeText(local);
        return true;
    }
    if (labelTool())
        return handleLabelPress(src);
    if (isLineTool()) {
        handleLinePress(src, mods);
        return true;
    }
    commitTextEditor();                    // drawing elsewhere bakes an open box
    startDraft(src);
    return true;
}

bool PinEditController::onMove(const QPoint& local, Qt::KeyboardModifiers mods)
{
    if (m_ocrActive) {
        if (m_ocrLayer.selecting()) {      // active drag → extend the selection + consume
            m_ocrLayer.extendSelect(toSource(local));
            m_pin->update();
            return true;
        }
        return false;                      // hover → let PinWindow apply the I-beam cursor
    }
    if (!toolActive())
        return false;
    m_lastLocal = local;                   // drives the badge preview under the cursor
    m_haveLocal = true;
    if (labelTool()) {
        // We consume the move (returning true below), so PinWindow's own cursor logic never
        // runs — pick the cursor here instead. Going through refreshLabelCursor also side-
        // steps PinWindow's "ArrowCursor means the tool doesn't own the cursor" sentinel,
        // which would otherwise swallow the plain arrow we want over the +/− buttons.
        refreshLabelCursor();
        const QPoint src = toSource(local);
        m_labelHoverOutline.clear();
        if (const annotation::Annotation* ed = editedLabel()) {
            // Keep the dashed outline on the badge being renumbered — the clear above
            // would otherwise drop it the moment the mouse twitches.
            m_labelHoverOutline = ed->outline();
            m_pin->update();
            return true;
        }
        if (m_labelPressId > 0) {
            const QPoint d = src - m_labelPressSrc;
            if (!m_labelMoved && d.manhattanLength() > 4)
                m_labelMoved = true;
            if (m_labelMoved && d != QPoint()) {
                // Translate directly (NOT via move): one undo command per mouse-move would
                // make a single drag take dozens of Ctrl+Z. The whole gesture is recorded
                // once on release — see handleLabelRelease.
                for (const auto& item : m_model->annotations()->items())
                    if (item->id == m_labelPressId) { item->translate(d); break; }
                m_labelDragTotal += d;
                m_labelPressSrc = src;
                refresh();
            }
        } else if (m_labelEditId <= 0) {
            if (annotation::Annotation* a = labelAt(src))
                m_labelHoverOutline = a->outline();
        }
        m_pin->update();                   // repaint the preview/outline as the cursor moves
        return true;
    }
    if (isLineTool() && m_linePhase != LinePhase::Idle) {
        handleLineMove(toSource(local), mods);
        return true;
    }
    if (!m_drawing)
        return false;
    updateDraft(toSource(local), mods);
    return true;
}

bool PinEditController::onRelease(const QPoint& local)
{
    if (m_ocrActive) {
        if (m_ocrLayer.selecting()) {
            m_ocrLayer.endSelect();
            m_pin->update();
        }
        return true;
    }
    if (!toolActive())
        return false;
    if (m_labelPressId > 0) { handleLabelRelease(); return true; }
    if (isLineTool() && m_linePhase != LinePhase::Idle) {
        handleLineRelease(toSource(local), QGuiApplication::keyboardModifiers());
        return true;
    }
    if (!m_drawing)
        return false;
    commitDraft();
    return true;
}

bool PinEditController::onDoubleClick(const QPoint& local)
{
    if (!toolActive() || !labelTool())
        return false;                      // not ours → the pin's own double-click rule
    flushLabelResize();
    handleLabelPress(toSource(local));
    return true;
}

bool PinEditController::onWheel(int steps)
{
    if (m_ocrActive)
        return true;                       // OCR: swallow the wheel (don't zoom while reading)
    if (!toolActive())
        return false;                      // no tool → the wheel still zooms the pin
    // Number label: scrolling over an existing badge resizes THAT badge; anywhere else it
    // falls through to the toolbar's size box (the next badge to be stamped).
    if (labelTool() && m_haveLocal) {
        const QPoint src = toSource(m_lastLocal);
        annotation::Annotation* a = labelAt(src);
        if (a) {
            beginLabelResize(a->id);           // snapshot the pre-gesture size (once/burst)
            // style.fontPx is source px; a wheel step is one SCREEN px, so scale it by the
            // same factor sourceStyle() uses.
            const qreal f = sourcePerScreen();
            a->style.fontPx = qBound(4.0 * f, a->style.fontPx + steps * f, 200.0 * f);
            if (a->id == m_labelEditId)        // keep the outline/buttons on the resized badge
                m_labelHoverOutline = a->outline();
            if (m_labelResizeFlush)
                m_labelResizeFlush->start();   // restart the idle window on every notch
            refresh();                         // committed badge changed → re-compose
            m_pin->update();
            return true;
        }
    }
    m_bar->wheelAdjustWidth(steps);
    return true;
}

// --- number label -------------------------------------------------------------

annotation::Annotation* PinEditController::labelAt(const QPoint& src) const
{
    const auto& items = m_model->annotations()->items();
    for (auto it = items.rbegin(); it != items.rend(); ++it) {   // topmost first
        annotation::Annotation* a = it->get();
        if (a->tool() == annotation::ToolId::NumberLabel && a->hitTest(src))
            return a;
    }
    return nullptr;
}

bool PinEditController::handleLabelPress(const QPoint& src)
{
    // The +/− buttons sit outside the badge but belong to the edit — check them first so
    // they neither stamp a new badge nor end the edit.
    const QVector<QRect> steps = labelStepButtons();
    if (steps.size() == 2) {
        if (steps[0].contains(src)) { stepLabel(+1); return true; }
        if (steps[1].contains(src)) { stepLabel(-1); return true; }
    }
    if (labelDeleteButton().contains(src)) { deleteLabel(); return true; }
    if (annotation::Annotation* a = labelAt(src)) {
        if (m_labelEditId > 0 && a->id != m_labelEditId)   // another badge → bank this edit
            commitLabelEdit();
        m_labelPressId = a->id;
        m_labelPressSrc = src;
        m_labelMoved = false;
        return true;
    }
    if (m_labelEditId > 0) commitLabelEdit();
    else                   placeLabel(src);
    return true;
}

void PinEditController::handleLabelRelease()
{
    const int id = m_labelPressId;
    const bool moved = m_labelMoved;
    const QPoint total = m_labelDragTotal;
    m_labelPressId = 0;
    m_labelMoved = false;
    m_labelDragTotal = QPoint();
    if (moved) {
        // The drag was applied live; record the whole gesture as ONE undo step.
        m_model->annotations()->moveApplied(id, total);
        refresh();
        syncUndoRedo();
        return;
    }
    for (const auto& item : m_model->annotations()->items())
        if (item->id == id) { beginLabelEdit(item.get()); break; }
}

void PinEditController::placeLabel(const QPoint& src)
{
    std::unique_ptr<annotation::Annotation> a =
        annotation::create(annotation::ToolId::NumberLabel, sourceStyle());
    if (!a)
        return;
    a->setGeometry({src});                 // the click point is the badge CENTRE
    a->setText(QString::number(m_nextLabel));
    if (m_model->annotations()->add(std::move(a)) > 0)
        ++m_nextLabel;
    m_labelHoverOutline.clear();
    refresh();
    syncUndoRedo();
    refreshLabelCursor();
}

void PinEditController::restartLabelCaret()
{
    // Typing shows a solid caret and restarts the blink, so it never blinks out mid-key.
    if (!m_labelCaret) {
        m_labelCaret = new QTimer(this);
        // Half the OS flash time = one on/off phase, matching system carets.
        m_labelCaret->setInterval(qMax(200, QGuiApplication::styleHints()->cursorFlashTime() / 2));
        connect(m_labelCaret, &QTimer::timeout, this, [this] {
            m_labelCaretOn = !m_labelCaretOn;
            m_pin->update();
        });
    }
    m_labelCaretOn = true;
    m_labelCaret->start();
}

void PinEditController::refreshLabelCursor()
{
    // PinWindow re-picks the cursor on mouse MOVE only, but stamping a badge and then
    // clicking it to renumber happens with the mouse held still — without this the
    // pointer would keep a stale (blanked) cursor. Snip has the same issue; see
    // SnipScreenView::applyCursor.
    if (labelTool() && m_haveLocal)
        m_pin->setCursor(cursorFor(m_lastLocal));
}

void PinEditController::beginLabelResize(int id)
{
    if (m_labelResizeId == id)
        return;                            // same badge, same burst — keep the first size
    flushLabelResize();                    // moved to a different badge: bank the old one
    const annotation::Annotation* a = nullptr;
    for (const auto& item : m_model->annotations()->items())
        if (item->id == id) { a = item.get(); break; }
    if (!a)
        return;
    m_labelResizeId = id;
    m_labelResizeBefore = a->style.fontPx;
    if (!m_labelResizeFlush) {
        m_labelResizeFlush = new QTimer(this);
        m_labelResizeFlush->setSingleShot(true);
        m_labelResizeFlush->setInterval(500);   // "the wheel stopped" — one gesture
        connect(m_labelResizeFlush, &QTimer::timeout, this, [this] { flushLabelResize(); });
    }
    m_labelResizeFlush->start();
}

void PinEditController::flushLabelResize()
{
    if (m_labelResizeId <= 0)
        return;
    const int id = m_labelResizeId;
    const qreal before = m_labelResizeBefore;
    m_labelResizeId = 0;
    if (m_labelResizeFlush)
        m_labelResizeFlush->stop();
    m_model->annotations()->resizeApplied(id, before);   // no-op if size ended unchanged
    refresh();
    syncUndoRedo();
}

annotation::Annotation* PinEditController::editedLabel() const
{
    if (m_labelEditId <= 0)
        return nullptr;
    for (const auto& item : m_model->annotations()->items())
        if (item->id == m_labelEditId)
            return item.get();
    return nullptr;
}

QVector<QRect> PinEditController::labelStepButtons() const
{
    const annotation::Annotation* a = editedLabel();
    if (!a)
        return {};
    // Fixed on-screen size: source px per screen px scales the button box so it doesn't
    // grow with the pin's zoom.
    const QRect b = a->bounds();
    const qreal f = sourcePerScreen();
    const int sz  = qMax(6, int(qRound(18 * f)));
    const int gap = qMax(1, int(qRound(4 * f)));
    const int x = b.right() + gap;
    const int cy = b.center().y();
    return { QRect(x, cy - sz - 1, sz, sz),      // + above the centre line
             QRect(x, cy + 1,      sz, sz) };    // − below it
}

QRect PinEditController::labelDeleteButton() const
{
    const annotation::Annotation* a = editedLabel();
    if (!a)
        return {};
    const QRect b = a->bounds();
    const qreal f = sourcePerScreen();
    const int sz  = qMax(6, int(qRound(18 * f)));
    const int gap = qMax(1, int(qRound(4 * f)));
    return QRect(b.center().x() - sz / 2, b.top() - gap - sz, sz, sz);
}

void PinEditController::deleteLabel()
{
    const annotation::Annotation* a = editedLabel();
    if (!a)
        return;
    AnnotationDocument* doc = m_model->annotations();
    const int id = a->id;
    bool ok = false;
    const int n = a->text().toInt(&ok);

    // What this edit session has changed so far (live, not yet on the undo stack) — banked
    // inside the macro so one Ctrl+Z restores the numbering as well as the badge.
    QHash<int, QString> cur;
    for (const auto& item : doc->items())
        if (item->tool() == annotation::ToolId::NumberLabel && m_labelBefore.contains(item->id))
            cur.insert(item->id, item->text());

    // Closing the gap: every remaining badge numbered above the deleted one drops by one.
    QHash<int, QString> gapBefore, gapAfter;
    if (ok) {
        for (const auto& item : doc->items()) {
            if (item->id == id || item->tool() != annotation::ToolId::NumberLabel)
                continue;
            bool k = false;
            const int v = item->text().toInt(&k);
            if (!k)
                continue;                       // a hand-typed non-number: leave it alone
            gapBefore.insert(item->id, item->text());
            gapAfter.insert(item->id, QString::number(v > n ? v - 1 : v));
        }
    }

    doc->beginMacro();
    doc->setTexts(m_labelBefore, cur);          // this session's renumbering (no-op if none)
    doc->remove(id);
    doc->setTexts(gapBefore, gapAfter);         // close the gap (no-op if nothing above)
    doc->endMacro();

    m_nextLabel = qMax(1, m_nextLabel - 1);

    // Leave edit mode without committing again (the macro already recorded everything).
    m_labelEditId = 0;
    m_labelBefore.clear();
    m_labelHoverOutline.clear();
    if (m_labelCaret)
        m_labelCaret->stop();
    refresh();
    syncUndoRedo();
    refreshLabelCursor();
}

int PinEditController::labelCount() const
{
    int n = 0;
    for (const auto& item : m_model->annotations()->items())
        if (item->tool() == annotation::ToolId::NumberLabel)
            ++n;
    return n;
}

void PinEditController::syncNextLabel(int labelsBefore)
{
    // Undoing a stamp takes the badge back, so the counter has to step back with it —
    // otherwise 1,2,3 followed by Ctrl+Z would stamp 4 next. The undo stack is opaque (any
    // command can be on top), so measure what actually happened: how many badges came or
    // went IS how far the counter moves. A renumber/move/restyle changes no count and so
    // leaves it alone, undoing a delete re-raises it (mirroring deleteLabel's decrement),
    // and the floor keeps a freshly re-picked tool's restart-at-1 intact.
    m_nextLabel = qMax(1, m_nextLabel + labelCount() - labelsBefore);
}

void PinEditController::syncNumberStyleFromDoc()
{
    // The numbering restyle is retroactive, so undoing it puts the badges back on their old
    // glyphs — and the toolbar + cursor preview have to follow, or the next stamp would come
    // out in a style nothing else on the pin uses.
    bool any = false;
    annotation::NumberStyle s = m_style.numberStyle;
    for (const auto& item : m_model->annotations()->items()) {
        if (item->tool() != annotation::ToolId::NumberLabel)
            continue;
        if (!any) { s = item->style.numberStyle; any = true; }
        else if (item->style.numberStyle != s) return;   // mixed (restored pin): leave it be
    }
    if (!any || s == m_style.numberStyle)
        return;
    m_style.numberStyle = s;
    if (m_draft)
        m_draft->style.numberStyle = s;
    m_bar->setNumberStyleValue(s);
}

void PinEditController::setNumberStyle(annotation::NumberStyle s)
{
    if (m_style.numberStyle == s)
        return;
    m_style.numberStyle = s;
    if (m_draft)
        m_draft->style.numberStyle = s;
    // The badges are one sequence, so the style applies to the whole set — including the
    // ones already stamped. Restyle them together so a single Ctrl+Z takes them all back.
    AnnotationDocument* doc = m_model->annotations();
    QVector<QPair<int, annotation::Style>> pending;   // collected first: restyle() mutates
    for (const auto& item : doc->items()) {
        if (item->tool() != annotation::ToolId::NumberLabel || item->style.numberStyle == s)
            continue;
        annotation::Style st = item->style;
        st.numberStyle = s;
        pending.push_back({item->id, st});
    }
    if (!pending.isEmpty()) {             // guarded: an empty macro would be a no-op undo step
        doc->beginMacro();
        for (const auto& p : pending)
            doc->restyle(p.first, p.second);
        doc->endMacro();
    }
    refresh();                            // the badges are composed into the pin's pixels
    syncUndoRedo();
    m_pin->update();
}

bool PinEditController::labelCanStep(int delta) const
{
    const annotation::Annotation* a = editedLabel();
    if (!a)
        return false;
    bool ok = false;
    const int n = a->text().toInt(&ok);
    if (!ok)
        return false;
    // Keep numbering inside the range the badges already span, so a step only ever
    // reorders existing numbers.
    int lo = n, hi = n;
    for (const auto& item : m_model->annotations()->items()) {
        if (item->tool() != annotation::ToolId::NumberLabel)
            continue;
        bool k = false;
        const int v = item->text().toInt(&k);
        if (!k) continue;
        lo = qMin(lo, v);
        hi = qMax(hi, v);
    }
    return delta > 0 ? n < hi : n > lo;
}

void PinEditController::stepLabel(int delta)
{
    if (!labelCanStep(delta))
        return;
    annotation::Annotation* a = editedLabel();
    if (!a)
        return;
    const int n = a->text().toInt();
    const int target = n + delta;
    // Hand our number to whichever badge holds the target — for a ±1 step that is exactly
    // "move this badge one place", leaving every other badge's relative order intact.
    for (const auto& item : m_model->annotations()->items()) {
        if (item.get() == a || item->tool() != annotation::ToolId::NumberLabel)
            continue;
        bool k = false;
        if (item->text().toInt(&k) == target && k) {
            item->setText(QString::number(n));
            break;
        }
    }
    a->setText(QString::number(target));
    m_labelEditFresh = false;
    restartLabelCaret();
    refresh();
}

void PinEditController::beginLabelEdit(annotation::Annotation* a)
{
    if (!a || a->tool() != annotation::ToolId::NumberLabel)
        return;
    // The badge STAYS in the document and is mutated live. Snapshot every badge's number:
    // a step also changes the badge it swaps with, and commit pushes the whole
    // before→after as ONE undo step.
    m_labelEditId = a->id;
    m_labelEditOrig = a->text();
    m_labelBefore.clear();
    for (const auto& item : m_model->annotations()->items())
        if (item->tool() == annotation::ToolId::NumberLabel)
            m_labelBefore.insert(item->id, item->text());
    m_labelEditFresh = true;
    m_labelHoverOutline = a->outline();
    refresh();
    syncUndoRedo();
    refreshLabelCursor();
    restartLabelCaret();
}

void PinEditController::commitLabelEdit()
{
    if (m_labelEditId <= 0)
        return;
    annotation::Annotation* a = editedLabel();
    // Deleting every digit is not how you remove a badge (that's the eraser) — an empty
    // edit reverts to the number it had.
    if (a && a->text().isEmpty())
        a->setText(m_labelEditOrig);

    // One undo step for the whole edit, covering the badge AND anything it swapped with.
    // The values are already applied (live display), so the command's redo is a no-op.
    QHash<int, QString> after;
    for (const auto& item : m_model->annotations()->items())
        if (item->tool() == annotation::ToolId::NumberLabel && m_labelBefore.contains(item->id))
            after.insert(item->id, item->text());
    m_model->annotations()->setTexts(m_labelBefore, after);

    m_labelEditId = 0;
    m_labelBefore.clear();
    m_labelHoverOutline.clear();
    if (m_labelCaret)
        m_labelCaret->stop();
    refresh();
    syncUndoRedo();
    refreshLabelCursor();
}

void PinEditController::cancelLabelEdit()
{
    if (m_labelEditId <= 0)
        return;
    // Esc reverts every badge the edit touched, and records nothing on the undo stack.
    for (const auto& item : m_model->annotations()->items())
        if (m_labelBefore.contains(item->id))
            item->setText(m_labelBefore.value(item->id));
    m_labelEditId = 0;
    m_labelBefore.clear();
    m_labelHoverOutline.clear();
    if (m_labelCaret)
        m_labelCaret->stop();
    refresh();
    syncUndoRedo();
    refreshLabelCursor();
}

bool PinEditController::handleLabelEditKey(int key)
{
    annotation::Annotation* a = editedLabel();
    if (!a)
        return false;
    if (key == Qt::Key_Escape)                        { cancelLabelEdit(); return true; }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) { commitLabelEdit(); return true; }
    if (key == Qt::Key_Up)                            { stepLabel(+1); return true; }
    if (key == Qt::Key_Down)                          { stepLabel(-1); return true; }
    if (key == Qt::Key_Delete)                        { deleteLabel(); return true; }
    if (key == Qt::Key_Backspace) {
        QString t = a->text();
        if (m_labelEditFresh) t.clear();
        else                  t.chop(1);
        m_labelEditFresh = false;
        a->setText(t);
        restartLabelCaret();
        refresh();
        return true;
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        // Typed numbers are RAW — no swap. +/− (and ↑/↓) are the tidy reorder.
        QString t = m_labelEditFresh ? QString() : a->text();
        m_labelEditFresh = false;
        if (t.size() < 4)                             // 4 digits is well past legible
            t += QChar('0' + (key - Qt::Key_0));
        a->setText(t);
        restartLabelCaret();
        refresh();
        return true;
    }
    return false;                                     // else fall through to the pin keys
}

bool PinEditController::onKey(int key, Qt::KeyboardModifiers mods)
{
    const bool ctrl = mods & Qt::ControlModifier;
    if (m_ocrActive) {                     // OCR: keys act on the recognized text
        if (key == Qt::Key_C && ctrl && !(mods & Qt::AltModifier)) {
            // Ctrl+C: copy the selected text and leave OCR (back to the annotate toolbar).
            if (m_ocrLayer.hasSelection()) {
                copyOcrText();
                m_bar->setOcrChecked(false);
                toggleOcr(false);
            }
            return true;
        }
        if (key == Qt::Key_A && ctrl) {    // Ctrl+A: select all recognized text
            m_ocrLayer.selectAll();
            m_pin->update();
            return true;
        }
        if (key == Qt::Key_Escape) {       // Esc: leave OCR, keep the pin + toolbar
            m_bar->setOcrChecked(false);
            toggleOcr(false);
            return true;
        }
        return true;                       // swallow other pin keys while reading text
    }
    // Renumbering a badge: digits/Backspace/Enter/Esc belong to it, not the pin shortcuts.
    if (handleLabelEditKey(key))
        return true;
    if (key == Qt::Key_Escape) {
        // An open text box handles Esc itself (its filter commits the box — only its ×
        // discards), so Esc only reaches us when no box has focus: close the toolbar,
        // ahead of the pin's own Esc = close (Features.md §3).
        emit closeRequested();
        return true;
    }
    if (ctrl && key == Qt::Key_Z) {
        commitTextEditor();
        m_model->annotations()->undo();
        refresh();
        syncUndoRedo();
        return true;
    }
    if (ctrl && (key == Qt::Key_Y)) {
        commitTextEditor();
        m_model->annotations()->redo();
        refresh();
        syncUndoRedo();
        return true;
    }
    return false;
}

Qt::CursorShape PinEditController::cursorFor(const QPoint& local) const
{
    if (m_ocrActive)
        return Qt::IBeamCursor;                                // text surface
    if (labelTool()) {
        // The renumber +/− buttons are chrome, not canvas — a normal arrow.
        const QPoint src = toSource(local);
        for (const QRect& r : labelStepButtons())
            if (r.contains(src))
                return Qt::ArrowCursor;
        if (labelDeleteButton().contains(src))
            return Qt::ArrowCursor;
        // The badge preview IS the cursor (paintDraft) — but only while a click would
        // stamp. Renumbering, or hovering a committed badge (where a click edits), draws
        // no preview, so the system cursor must come back or the pointer would vanish.
        if (m_labelEditId > 0 || labelAt(src))
            return Qt::IBeamCursor;
        return Qt::BlankCursor;
    }
    return toolActive() ? Qt::CrossCursor : Qt::ArrowCursor;   // Arrow = "not mine"
}

// --- drafting ----------------------------------------------------------------

void PinEditController::startDraft(const QPoint& src)
{
    m_draft = annotation::create(m_tool, sourceStyle());
    if (!m_draft) { m_drawing = false; return; }
    // Snapshot what a redaction/eraser draft samples: the committed layer cannot change
    // mid-stroke, so this is taken once per stroke rather than per mouse-move.
    m_draftClean = m_model->processedSource();       // eraser restores real pixels
    m_draftComposite = m_model->annotatedSource();   // redaction covers committed items too
    m_drawing = true;
    m_draftAnchor = src;
    m_draftPts = {src};
    m_draft->setGeometry(m_draftPts);
    setBarsVisible(false);                           // the bars get out of the way
    m_pin->update();
}

void PinEditController::updateDraft(const QPoint& src, Qt::KeyboardModifiers mods)
{
    if (!m_drawing || !m_draft)
        return;
    using annotation::ToolId;
    const bool shift = mods & Qt::ShiftModifier;
    const bool ctrl  = mods & Qt::ControlModifier;
    const ToolId t = m_draft->tool();
    const bool isPoly = t == ToolId::Pencil || t == ToolId::Marker || t == ToolId::LineStrip
                     || t == ToolId::Eraser || t == ToolId::MosaicBrush || t == ToolId::BlurBrush;
    if (isPoly) {
        // Pen + highlighter straighten a freehand stroke from the anchor: Shift follows
        // any angle, Ctrl locks to 45° steps. Ctrl wins if both are held. Kept in step
        // with SnipOverlay::updateDraft.
        const bool straighten = (shift || ctrl) && (t == ToolId::Pencil || t == ToolId::Marker);
        if (straighten)
            m_draftPts = {m_draftAnchor,
                          ctrl ? ShapeConstraints::snapLine(m_draftAnchor, src) : src};
        else
            m_draftPts.append(src);
    } else {
        // Ctrl squares the rect family, Shift snaps a line/arrow to 45°.
        m_draftPts = {m_draftAnchor,
                      ShapeConstraints::constrainedEnd(t, m_draftAnchor, src, mods)};
    }
    m_draft->setGeometry(m_draftPts);
    m_pin->update();
}

void PinEditController::commitDraft()
{
    m_drawing = false;
    if (m_draft) {
        m_model->annotations()->add(std::move(m_draft));   // ignored if degenerate
        m_draft.reset();
    }
    m_draftClean = QImage();
    m_draftComposite = QImage();
    setBarsVisible(true);
    refresh();
    syncUndoRedo();
}

void PinEditController::paintDraft(QPainter& p) const
{
    // OCR text chrome (grey plates + selection) over the composed pin. Source-px word
    // boxes map through the live source→widget transform, so they track the pin's
    // rotation/flip/scale (all 90°-multiple, so mapRect stays exact).
    if (m_ocrActive && !m_ocrLayer.isEmpty()) {
        const QTransform t = m_pin->sourceToWidget();
        auto mapRect = [t](const QRect& r) -> QRectF { return t.mapRect(QRectF(r)); };
        m_ocrLayer.paint(p, mapRect, PinWindow::themeColor());
    }
    // Number label: a dashed outline round the badge under the cursor (or the one being
    // renumbered), plus the WYSIWYG preview of the badge the next click will stamp —
    // drawn in SCREEN units at m_style's size, which is exactly what sourceStyle() will
    // scale it to when committed.
    if (labelTool()) {
        if (!m_labelHoverOutline.isEmpty()) {
            const QTransform t = m_pin->sourceToWidget();
            QPolygonF poly;
            for (const QPoint& pt : m_labelHoverOutline)
                poly << t.map(QPointF(pt));
            p.save();
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(PinWindow::themeColor(), 1.2, Qt::DashLine));
            p.drawPolygon(poly);
            p.restore();
        }
        // Preview only where a click would actually stamp (not over a badge you'd edit,
        // nor over the +/− chrome).
        bool onChrome = false;
        if (m_haveLocal) {
            const QPoint src = toSource(m_lastLocal);
            for (const QRect& r : labelStepButtons())
                if (r.contains(src)) { onChrome = true; break; }
            if (labelDeleteButton().contains(src))
                onChrome = true;
        }
        if (m_haveLocal && m_labelEditId <= 0 && m_labelHoverOutline.isEmpty() && !onChrome)
            annotation::paintNumberBadge(p, m_style, QPointF(m_lastLocal),
                                         QString::number(m_nextLabel));

        // The badge being renumbered is a COMMITTED annotation (already baked into the
        // composed pin), so its caret and step buttons are drawn here rather than with
        // the draft below.
        if (const annotation::Annotation* ed = editedLabel()) {
            const QTransform t = m_pin->sourceToWidget();
            if (m_labelCaretOn && !ed->geometry().isEmpty()) {
                p.save();
                p.setTransform(t, true);
                p.fillRect(annotation::numberBadgeCaretRect(
                               ed->style, QPointF(ed->geometry().first()), ed->text()),
                           annotation::numberBadgeInk(ed->style));
                p.restore();
            }
            // +/− step buttons: a step swaps numbers with the badge holding the target,
            // reordering without disturbing the others. Greyed at the ends of the range.
            const QVector<QRect> steps = labelStepButtons();
            const QRect del = labelDeleteButton();
            if (steps.size() == 2 || del.isValid()) {
                p.save();
                p.setRenderHint(QPainter::Antialiasing, true);
                auto plate = [&](const QRectF& r, const QColor& edge, bool on) {
                    p.setPen(QPen(QColor(255, 255, 255, 220), 2.0));
                    p.setBrush(Qt::NoBrush);
                    p.drawRoundedRect(r, 3, 3);
                    p.setPen(QPen(on ? edge : QColor(150, 150, 150), 1.2));
                    p.setBrush(QColor(255, 255, 255, on ? 235 : 140));
                    p.drawRoundedRect(r, 3, 3);
                };
                for (int i = 0; i < steps.size(); ++i) {
                    const int delta = (i == 0) ? +1 : -1;
                    const bool on = labelCanStep(delta);
                    const QRectF r = t.mapRect(QRectF(steps[i]));
                    plate(r, PinWindow::themeColor(), on);
                    const QPointF c = r.center();
                    const qreal a = r.width() * 0.28;
                    p.setPen(QPen(on ? QColor(40, 40, 40) : QColor(160, 160, 160), 1.6));
                    p.drawLine(QPointF(c.x() - a, c.y()), QPointF(c.x() + a, c.y()));
                    if (delta > 0)
                        p.drawLine(QPointF(c.x(), c.y() - a), QPointF(c.x(), c.y() + a));
                }
                if (del.isValid()) {
                    // × above the outline, in red — it destroys the badge.
                    const QColor red(0xc0, 0x39, 0x2b);
                    const QRectF r = t.mapRect(QRectF(del));
                    plate(r, red, true);
                    const QPointF c = r.center();
                    const qreal a = r.width() * 0.24;
                    p.setPen(QPen(red, 1.8, Qt::SolidLine, Qt::RoundCap));
                    p.drawLine(QPointF(c.x() - a, c.y() - a), QPointF(c.x() + a, c.y() + a));
                    p.drawLine(QPointF(c.x() + a, c.y() - a), QPointF(c.x() - a, c.y() + a));
                }
                p.restore();
            }
        }
    }

    if (!m_draft)
        return;
    // Committed annotations are already inside the composed image (PinModel::compose
    // bakes them); only the live draft is drawn on top, in the same source-px space.
    annotation::PaintCtx ctx;
    ctx.base = &m_draftClean;                // eraser restores the clean source
    ctx.redactSource = &m_draftComposite;    // mosaic/blur cover the committed layer too
    ctx.originLogical = QPointF(0, 0);       // annotation geometry IS source px
    ctx.scale = 1.0;

    p.save();
    p.setTransform(m_pin->sourceToWidget(), true);
    m_draft->paint(p, ctx);
    p.restore();

    // Line control points, on top of the draft. Drawn in widget coords at a fixed
    // on-screen size (not through the transform) so they stay grabbable at any pin
    // scale — matching lineControlHit()'s screen-px radius, and the same chrome
    // SnipScreenView draws for the snip overlay.
    if (m_linePhase == LinePhase::Controlling) {
        const QTransform t = m_pin->sourceToWidget();
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setBrush(Qt::white);
        p.setPen(QPen(PinWindow::themeColor(), 1.5));
        for (const QPoint& v : m_lineVerts) {
            const QPointF l = t.map(QPointF(v));
            p.drawRect(QRectF(l.x() - 4, l.y() - 4, 8, 8));
        }
        p.restore();
    }
}

void PinEditController::refresh()
{
    m_pin->refreshComposed();
}

void PinEditController::syncUndoRedo()
{
    AnnotationDocument* doc = m_model->annotations();
    m_bar->setUndoRedoState(doc->canUndo(), doc->canRedo());
}

// --- Line tool: build → control → finalize -----------------------------------

void PinEditController::rebuildLineDraft(const QPoint* floating)
{
    if (!m_draft)
        return;
    QVector<QPoint> pts = m_lineVerts;
    if (floating)
        pts.append(*floating);
    if (pts.size() == 1)
        pts.append(pts.first());     // keep a valid (if zero-length) 2-point geometry
    m_draft->style = sourceStyle();
    m_draft->setGeometry(pts);
}

int PinEditController::lineControlHit(const QPoint& src) const
{
    int idx = -1;
    double best = 10.0 * sourcePerScreen();      // ~10 screen px, in source units
    for (int i = 0; i < m_lineVerts.size(); ++i) {
        const double d = std::hypot(double(m_lineVerts[i].x() - src.x()),
                                    double(m_lineVerts[i].y() - src.y()));
        if (d < best) { best = d; idx = i; }
    }
    return idx;
}

QPoint PinEditController::lineSnap(const QPoint& from, const QPoint& src,
                                   Qt::KeyboardModifiers mods)
{
    return ShapeConstraints::constrainedEnd(annotation::ToolId::Line, from, src, mods);
}

void PinEditController::handleLinePress(const QPoint& src, Qt::KeyboardModifiers mods)
{
    switch (m_linePhase) {
        case LinePhase::Idle:                     // first press: start building
            m_linePhase = LinePhase::Building;
            m_lineVerts = {src};
            m_linePressPos = src;
            m_lineDragArmed = true;               // a drag from here = single segment
            m_lineDragging = false;
            m_draft = annotation::create(annotation::ToolId::Line, sourceStyle());
            m_draftClean = m_model->processedSource();
            m_draftComposite = m_model->annotatedSource();
            rebuildLineDraft(&src);
            break;
        case LinePhase::Building: {                // each click adds a vertex
            // Snap the placed vertex, not just the preview, or the committed line
            // would differ from what the preview showed.
            const QPoint v = lineSnap(m_lineVerts.last(), src, mods);
            m_lineVerts.append(v);
            rebuildLineDraft(&v);
            break;
        }
        case LinePhase::Controlling:              // grab a control point to drag
            m_dragVert = lineControlHit(src);
            break;
    }
    m_pin->update();
}

void PinEditController::handleLineMove(const QPoint& src, Qt::KeyboardModifiers mods)
{
    if (m_linePhase == LinePhase::Building) {
        if (m_lineDragArmed && !m_lineDragging) {
            // Measured on the raw cursor: this asks "did the mouse move?", not "where
            // does the segment end?".
            const double moved = std::hypot(double(src.x() - m_linePressPos.x()),
                                            double(src.y() - m_linePressPos.y()));
            if (moved > kLineDragSlop * sourcePerScreen())
                m_lineDragging = true;            // press moved enough → it's a drag
        }
        const QPoint end = lineSnap(m_lineVerts.last(), src, mods);
        rebuildLineDraft(&end);                   // cursor previews the next segment
    } else if (m_linePhase == LinePhase::Controlling && m_dragVert >= 0) {
        // Snap a control point only on a single-segment line, where the opposite end
        // is an unambiguous anchor. A middle vertex has two neighbours — no one angle.
        m_lineVerts[m_dragVert] = m_lineVerts.size() == 2
                                      ? lineSnap(m_lineVerts.at(1 - m_dragVert), src, mods)
                                      : src;
        rebuildLineDraft(nullptr);
    }
    m_pin->update();
}

void PinEditController::handleLineRelease(const QPoint& src, Qt::KeyboardModifiers mods)
{
    if (m_linePhase == LinePhase::Building) {
        if (m_lineDragArmed && m_lineDragging) {
            // A drag (not a click) means a single-segment line: skip building and go
            // straight to the control phase.
            m_lineVerts = {m_linePressPos, lineSnap(m_linePressPos, src, mods)};
            m_dragVert = -1;
            m_linePhase = LinePhase::Controlling;
            rebuildLineDraft(nullptr);
        }
        m_lineDragArmed = false;                  // a plain click → multi-segment mode
        m_lineDragging = false;
    } else if (m_linePhase == LinePhase::Controlling) {
        m_dragVert = -1;                          // drop the dragged control point
    }
    m_pin->update();
}

bool PinEditController::handleLineSecondary()
{
    if (!isLineTool() || m_linePhase == LinePhase::Idle)
        return false;                             // → the pin opens its context menu

    if (m_linePhase == LinePhase::Building) {
        if (m_lineVerts.size() >= 2) {            // enough for a line → edit handles
            m_linePhase = LinePhase::Controlling;
            m_dragVert = -1;
            rebuildLineDraft(nullptr);            // drop the floating preview point
        } else {
            resetLineState();                     // a single click is not a line
        }
    } else {                                      // Controlling → finalize
        if (m_draft) {
            rebuildLineDraft(nullptr);
            m_model->annotations()->add(std::move(m_draft));
            m_draft.reset();
        }
        m_linePhase = LinePhase::Idle;
        m_lineVerts.clear();
        m_dragVert = -1;
        refresh();
        syncUndoRedo();
    }
    m_pin->update();
    return true;
}

void PinEditController::resetLineState()
{
    m_linePhase = LinePhase::Idle;
    m_lineVerts.clear();
    m_dragVert = -1;
    m_lineDragArmed = false;
    m_lineDragging = false;
    if (isLineTool())
        m_draft.reset();
}

// --- Text (place-only: committed pin text is not re-editable) ----------------

void PinEditController::placeText(const QPoint& local)
{
    commitTextEditor();                 // an open box bakes before a new one starts

    m_textEditor = new TextBoxEditor(m_pin);
    m_textEditor->setTextColor(m_style.color);
    m_textEditor->setFontFamily(m_style.fontFamily);
    m_textEditor->setFontPixelSize(m_style.fontPx);   // the editor lives in screen coords
    m_textEditor->setBold(m_style.bold);
    m_textEditor->setItalic(m_style.italic);
    m_textEditor->setHollow(m_style.hollowText);
    m_textEditor->setContentBox(QRect(local, QSize(kDefTextW, kDefTextH)));

    connect(m_textEditor, &TextBoxEditor::committed, this, [this] { commitTextEditor(); });
    connect(m_textEditor, &TextBoxEditor::canceled, this, [this] { cancelTextEditor(); });
    connect(m_textEditor, &TextBoxEditor::fontSizeChanged, this, [this](qreal px) {
        m_style.fontPx = px;
        m_bar->setFontSizeValue(px);    // reflect a corner-drag/wheel scale (no echo)
    });

    m_textEditor->show();
    m_textEditor->raise();
    m_textEditor->focusEditor();
}

void PinEditController::commitTextEditor()
{
    if (!m_textEditor)
        return;
    TextBoxEditor* ed = m_textEditor;
    m_textEditor = nullptr;

    const QString text = ed->text();
    if (!text.trimmed().isEmpty()) {
        // Use the inner text rect (not the chrome box) so the baked text lands exactly
        // where the editor previewed it. Work from its CENTRE and size: a TextShape lays
        // its text out in an upright box and rotates about the centre, so a screen-shaped
        // box maps onto a rotated pin without ever being transposed.
        const QRectF inner(ed->textRect());
        const qreal f = sourcePerScreen();
        // QRectF's centre, NOT QRect::center() — the latter is integer arithmetic
        // (x + (w-1)/2), so it sits ~1px up-left of the true centre and reconstructing
        // the top-left from it drifts the baked text up-left of the editor's preview.
        const QPointF cSrc = m_pin->sourceToWidget().inverted().map(inner.center());
        const QSize szSrc(qMax(1, int(qRound(inner.width() * f))),
                          qMax(1, int(qRound(inner.height() * f))));

        annotation::Style st = m_style;
        st.fontPx = ed->fontPixelSize() * f;
        std::unique_ptr<annotation::Annotation> a =
            annotation::create(annotation::ToolId::Text, st);
        if (a) {
            a->setGeometry({QPoint(qRound(cSrc.x() - szSrc.width() / 2.0),
                                   qRound(cSrc.y() - szSrc.height() / 2.0))});
            a->setBox(szSrc);
            a->setRotation(sourceRotationFor(ed->rotationDeg()));
            a->setMirrored(mirroredForPin());     // survive the pin's flip, readable
            a->setText(text);
            m_model->annotations()->add(std::move(a));   // ignored if empty
        }
    }
    ed->deleteLater();
    m_pin->setFocus();
    refresh();
    syncUndoRedo();
}

void PinEditController::cancelTextEditor()
{
    if (!m_textEditor)
        return;
    m_textEditor->deleteLater();
    m_textEditor = nullptr;
    m_pin->setFocus();
    m_pin->update();
}

// --- toolbar placement -------------------------------------------------------

void PinEditController::setBarsVisible(bool on)
{
    if (!on) {
        m_bar->hide();
        if (m_subBar) m_subBar->hide();
        if (m_ocrBar) m_ocrBar->hide();
        return;
    }
    reposition();
}

void PinEditController::reposition()
{
    if (!m_bar || !m_pin->isVisible())
        return;
    m_bar->adjustSize();

    // The second row is either a tool's sub-panel OR the OCR bar (mutually exclusive with
    // the drawing tools). Hide whichever one is not the active row.
    const bool showSub = m_bar->subBarVisible() && !m_ocrActive;
    QWidget* row2 = m_ocrActive ? static_cast<QWidget*>(m_ocrBar)
                                : (showSub ? m_subBar : nullptr);
    if (m_subBar && row2 != m_subBar) m_subBar->hide();
    if (m_ocrBar && row2 != m_ocrBar) m_ocrBar->hide();
    if (row2) row2->adjustSize();

    const QRect pinGeo = m_pin->frameGeometry();
    const QRect screen = m_pin->screen() ? m_pin->screen()->availableGeometry() : QRect();
    const int mw = m_bar->width(), mh = m_bar->height();
    const int sh = row2 ? row2->height() : 0;
    const int blockH = mh + (row2 ? kRowGap + sh : 0);

    // Anchor the (main, row2) stack below the pin, right-aligned; flip above when there is
    // no room. row2 left-aligns to the main bar so it stays put as its width changes.
    int yTop = pinGeo.bottom() + kGap;
    if (!screen.isNull() && yTop + blockH > screen.bottom())
        yTop = pinGeo.top() - kGap - blockH;
    int xMain = pinGeo.right() - mw;
    if (!screen.isNull()) {
        xMain = qBound(screen.left(), xMain, qMax(screen.left(), screen.right() - mw));
        yTop = qBound(screen.top(), yTop, qMax(screen.top(), screen.bottom() - blockH));
    }

    m_bar->move(xMain, yTop);
    m_bar->show();
    m_bar->raise();
    if (row2) {
        row2->move(xMain, yTop + mh + kRowGap);
        row2->show();
        row2->raise();
    }
}
