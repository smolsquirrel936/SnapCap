#pragma once

#include <QColor>
#include <QWidget>

class QLabel;
class QPushButton;

// The 2nd-level scrolling-capture toolbar, shown for the whole session from the moment
// the row-1 button is pressed (mirrors PaintBar's sub-bar look, like OcrBar).
// Contents: captured size · Direction · Move · Auto ‖ Trim-start · Trim-end ‖ Done ·
// Cancel, all seven icon-only (code-drawn glyphs, named by their tooltips) · then,
// trailing them all, the message that only appears when there is something to say.
// Pure presentation — the host controller (SnipOverlay) owns the session and the image.
class ScrollCaptureBar : public QWidget {
    Q_OBJECT
public:
    explicit ScrollCaptureBar(QWidget* parent = nullptr);

    void retranslate();
    void setAccent(const QColor& c);        // UI accent for hover/pressed chrome

    // Size of the stitched result so far, in capture px. The width is the region's and
    // never changes; the height is the session's progress.
    void setCapturedSize(int w, int h);
    // The one message slot: the "scroll back" warning in red, or a neutral
    // status line such as auto-scroll reporting that it reached the end. Empty hides it.
    void setMessage(const QString& text, bool warning);
    // Done is meaningless until something has actually been captured.
    void setDoneEnabled(bool on);

    // Auto-scroll toggle. setAutoChecked() un-checks it when the session stops scrolling
    // by itself (end of page, pointer left the region, the user took the wheel) without
    // re-emitting.
    void setAutoChecked(bool on);
    // Move mode: drag the capture area itself, to reach content the region never covered
    // (the top of a page you started capturing halfway down). setMoveChecked() reflects
    // the host leaving the mode by other means (Esc) without re-emitting. The glyph is an
    // open palm that closes into a fist while the mode is on, so the button says "you can
    // grab the area" and then "you are holding it".
    void setMoveChecked(bool on);

    // Trim: throw away everything on one side of the region you are looking at — the
    // start of the capture axis (top ↕ / left ↔) or its end. Enabled per side, since a
    // side with nothing on it has nothing to cut; the glyph is the PIECE REMOVED, a block
    // torn off along the cut, and follows the axis with setDirection().
    void setTrimEnabled(bool start, bool end);

    // Capture axis. Not a checkable mode: the glyph IS the state — a double-headed arrow
    // pointing the way the capture will grow — and clicking flips it. The host persists
    // the choice to `scroll/direction` and runs a horizontal session by transposing every
    // frame into the same vertical matcher (see SnipOverlay::startScrollCapture).
    void setDirection(bool horizontal);
    // Locked once the capture has an axis of its own to contradict — see the host.
    void setDirectionEnabled(bool on);

protected:
    // Pops each button's hint the moment the pointer arrives, instead of waiting for Qt's
    // automatic tooltip — see the note on the implementation.
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    void doneRequested();
    void cancelRequested();
    void autoScrollToggled(bool on);
    void moveModeToggled(bool on);
    void directionToggled(bool horizontal);
    void trimStartRequested();
    void trimEndRequested();

private:
    QString barQss() const;

    void applyMoveIcon();                   // open palm / closed fist, by checked state
    void showHint(QWidget* w) const;        // the hover hint box, at once rather than on a delay
    void applyDirectionHint();              // axis + locked state pick one of four texts
    void applyTrimHints();                  // axis + enabled state pick one of three

    QLabel*      m_size = nullptr;
    QLabel*      m_message = nullptr;
    QPushButton* m_dir = nullptr;
    QPushButton* m_move = nullptr;
    QPushButton* m_auto = nullptr;
    QPushButton* m_trimStart = nullptr;     // cuts the top (↕) / left (↔) piece away
    QPushButton* m_trimEnd = nullptr;       // cuts the bottom (↕) / right (↔) piece away
    QPushButton* m_done = nullptr;
    QPushButton* m_cancel = nullptr;
    QColor       m_accent;
    bool         m_horizontal = false;
    int          m_w = 0;
    int          m_h = 0;
};
