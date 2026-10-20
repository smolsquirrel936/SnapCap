#pragma once

#include <QColor>
#include <QWidget>

class QPushButton;
class QLabel;

// The timecode field on the recording bar. ONE field serves both jobs:
//   * idle      — an editable delay, shown negative: "-00:05" means "start in 5s".
//                 Scroll it or click and type to change; "-00:00" = start at once.
//   * counting  — the delay ticking down: -00:05 … -00:01 … 00:00 → recording starts.
//   * recording — the elapsed clock counting up: 00:01, 00:02, … 1:02:03.
// Editable only while idle, so a running clock can't be typed over.
class RecordTimerField : public QWidget {
    Q_OBJECT
public:
    explicit RecordTimerField(QWidget* parent = nullptr);

    int  delaySeconds() const { return m_delay; }
    void setDelaySeconds(int s);
    void setEditable(bool on);                 // false while counting down / recording
    void showMilliseconds(qint64 ms);          // elapsed (>=0, counts up)
    void showCountdown(int secondsLeft);       // remaining delay (renders negative)
    void showDelay();                          // back to the editable "-MM:SS"

    QSize sizeHint() const override;

signals:
    void delayChanged(int seconds);

protected:
    void paintEvent(QPaintEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void focusOutEvent(QFocusEvent*) override;

private:
    void commitTyped();

    int  m_delay = 0;              // configured delay, seconds
    QString m_text;                // what is actually painted
    bool m_editable = true;
    bool m_typing = false;         // click-to-type mode
    QString m_typed;               // digits collected while typing
    QColor m_accent;
};

// The 2nd-level RECORDING toolbar: [Record/Stop] [Pause/Resume] [timer].
//
// Mirrors OcrBar's role — a standalone framed row the host controller positions —
// but unlike OCR, recording is NOT exclusive with the drawing tools: you can arm an
// annotation tool while recording, at which point the host moves this bar to a THIRD
// row beneath the tool's own sub-bar.
class RecordBar : public QWidget {
    Q_OBJECT
public:
    // Idle → (Counting) → Recording ⇄ Paused. Stopping returns to Idle only in
    // principle: in practice the host ends the whole session on stop.
    enum class State { Idle, Counting, Recording, Paused };

    explicit RecordBar(QWidget* parent = nullptr);

    void retranslate();
    void setAccent(const QColor& c);

    State state() const { return m_state; }
    void setState(State s);
    int  delaySeconds() const;
    void setDelaySeconds(int s);

    void setElapsed(qint64 ms);            // drives the timer while recording
    void setCountdown(int secondsLeft);    // drives it while counting down

signals:
    void startRequested();                 // Record pressed while Idle
    void stopRequested();                  // Stop pressed while Counting/Recording/Paused
    void pauseToggled(bool paused);
    void delayChanged(int seconds);

private:
    QString barQss() const;
    void refreshButtons();

    QPushButton* m_record = nullptr;       // Record ⇄ Stop
    QPushButton* m_pause = nullptr;        // Pause ⇄ Resume
    RecordTimerField* m_timer = nullptr;
    State m_state = State::Idle;
    QColor m_accent;
};
