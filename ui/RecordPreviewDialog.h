#pragma once

#include <QDialog>
#include <QImage>
#include <memory>

namespace platform { class IMediaPlayer; }
class QLabel;
class QPushButton;

// The seek bar. Carries three handles: the playhead plus a trim IN and OUT marker.
// Drag the bar to scrub, drag a marker to set the range; the region outside the
// range is dimmed so the kept part is obvious at a glance.
class TrimSlider : public QWidget {
    Q_OBJECT
public:
    explicit TrimSlider(QWidget* parent = nullptr);

    void setDuration(qint64 ms);
    void setPosition(qint64 ms);          // playhead (does not emit)
    qint64 trimStart() const { return m_in; }
    qint64 trimEnd() const { return m_out; }
    bool trimmed() const;                 // is the range narrower than the whole clip?
    void resetTrim();

    QSize sizeHint() const override;

signals:
    void scrubbed(qint64 ms);             // user moved the playhead
    void trimChanged();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    enum class Grab { None, Playhead, In, Out };
    Grab grabAt(int x) const;
    int  xFor(qint64 ms) const;
    qint64 msFor(int x) const;

    qint64 m_duration = 0;
    qint64 m_pos = 0;
    qint64 m_in = 0;
    qint64 m_out = 0;
    Grab m_grab = Grab::None;
};

// Shown after a recording stops (Preferences → Recording → "Show preview"). The file
// is ALREADY saved by the time this opens — this is a review window, not a save
// gate, so closing it can never lose a take.
//
// Trimming writes a NEW file next to the original and leaves the original alone.
class RecordPreviewDialog : public QDialog {
    Q_OBJECT
public:
    RecordPreviewDialog(const QString& path, QWidget* parent = nullptr);
    ~RecordPreviewDialog() override;

protected:
    void resizeEvent(QResizeEvent* e) override;
    void changeEvent(QEvent* e) override;      // FontChange -> re-derive the hint font

private:
    // The hint sits one point under the body text, derived from the dialog's own font so
    // it follows interface/font. It needs an explicit setFont (an inline stylesheet
    // font-size would beat the inherited font and pin it), and an explicit font does not
    // inherit — hence changeEvent re-running this when the interface font changes.
    void applyHintFont();
    void showFrame(const QImage& img);
    void togglePlay();
    void saveTrimmed();
    void retranslate();
    void updateTransport();
    // Pin the hint label to the height of the TALLEST message it can show at the current
    // width. The idle and trimmed hints wrap to different line counts, and the video label
    // is the only stretching widget — so without this, dragging a trim marker steals two
    // lines from the video and the picture visibly jumps.
    void reserveHintHeight();

    QString m_path;
    // Newest trimmed copy written this session, so "Show in folder" lands on the file you
    // just made rather than the original. Empty until the first successful save.
    QString m_lastSaved;
    std::unique_ptr<platform::IMediaPlayer> m_player;
    QLabel* m_video = nullptr;
    QLabel* m_time = nullptr;
    QLabel* m_hint = nullptr;
    QPushButton* m_play = nullptr;
    QPushButton* m_saveTrim = nullptr;
    QPushButton* m_showFolder = nullptr;
    QPushButton* m_close = nullptr;
    TrimSlider* m_slider = nullptr;
    QImage m_lastFrame;
};
