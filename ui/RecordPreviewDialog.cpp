#include "ui/RecordPreviewDialog.h"

#include "core/i18n/I18n.h"
#include "core/recording/RecordingSpec.h"
#include "platform/platform.h"
#include "ui/PrefsStyle.h"

#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {
constexpr int kHandleW = 9;
constexpr int kBarH = 10;
}

// ------------------------------------------------------------------ TrimSlider

TrimSlider::TrimSlider(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumHeight(34);
}

QSize TrimSlider::sizeHint() const { return QSize(480, 34); }

void TrimSlider::setDuration(qint64 ms)
{
    m_duration = qMax<qint64>(0, ms);
    m_in = 0;
    m_out = m_duration;
    update();
}

void TrimSlider::setPosition(qint64 ms)
{
    m_pos = qBound<qint64>(0, ms, m_duration);
    update();
}

bool TrimSlider::trimmed() const
{
    // A few ms of slop: dragging a handle to "the end" should not count as a trim.
    return m_duration > 0 && (m_in > 20 || m_out < m_duration - 20);
}

void TrimSlider::resetTrim()
{
    m_in = 0;
    m_out = m_duration;
    update();
}

int TrimSlider::xFor(qint64 ms) const
{
    if (m_duration <= 0)
        return kHandleW;
    const int usable = width() - 2 * kHandleW;
    return kHandleW + int(qRound(double(ms) / double(m_duration) * usable));
}

qint64 TrimSlider::msFor(int x) const
{
    const int usable = qMax(1, width() - 2 * kHandleW);
    const double f = double(qBound(kHandleW, x, width() - kHandleW) - kHandleW) / usable;
    return qint64(qRound(f * double(m_duration)));
}

TrimSlider::Grab TrimSlider::grabAt(int x) const
{
    // Trim handles win ties: they are what you are aiming for when you click near one.
    if (qAbs(x - xFor(m_in)) <= kHandleW)  return Grab::In;
    if (qAbs(x - xFor(m_out)) <= kHandleW) return Grab::Out;
    return Grab::Playhead;
}

void TrimSlider::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    m_grab = grabAt(int(e->position().x()));
    mouseMoveEvent(e);
}

void TrimSlider::mouseMoveEvent(QMouseEvent* e)
{
    const int x = int(e->position().x());
    if (m_grab == Grab::None) {
        setCursor(grabAt(x) == Grab::Playhead ? Qt::PointingHandCursor : Qt::SizeHorCursor);
        return;
    }
    const qint64 ms = msFor(x);
    switch (m_grab) {
        case Grab::In:
            m_in = qBound<qint64>(0, ms, m_out - 100);   // keep at least 100 ms
            emit trimChanged();
            break;
        case Grab::Out:
            m_out = qBound<qint64>(m_in + 100, ms, m_duration);
            emit trimChanged();
            break;
        case Grab::Playhead:
            m_pos = ms;
            emit scrubbed(m_pos);
            break;
        default:
            break;
    }
    update();
}

void TrimSlider::mouseReleaseEvent(QMouseEvent*)
{
    m_grab = Grab::None;
}

void TrimSlider::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor accent = PrefsStyle::accentColor();
    const int y = (height() - kBarH) / 2;
    const QRect bar(kHandleW, y, width() - 2 * kHandleW, kBarH);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xd8, 0xd8, 0xd8));
    p.drawRoundedRect(bar, 4, 4);

    // The kept range, drawn bright against the dimmed remainder.
    const QRect keep(xFor(m_in), y, qMax(2, xFor(m_out) - xFor(m_in)), kBarH);
    p.setBrush(accent.lighter(150));
    p.drawRoundedRect(keep, 4, 4);

    // Trim handles.
    p.setBrush(accent);
    for (qint64 ms : {m_in, m_out}) {
        const int hx = xFor(ms);
        p.drawRoundedRect(QRect(hx - kHandleW / 2, y - 5, kHandleW, kBarH + 10), 3, 3);
    }

    // Playhead on top of everything.
    const int px = xFor(m_pos);
    p.setBrush(QColor(0x20, 0x20, 0x20));
    p.drawRoundedRect(QRect(px - 2, y - 8, 4, kBarH + 16), 2, 2);
}

// ---------------------------------------------------------- RecordPreviewDialog

RecordPreviewDialog::RecordPreviewDialog(const QString& path, QWidget* parent)
    : QDialog(parent), m_path(path)
{
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    // Always on top: the preview opens by itself when a recording finishes, and it is
    // parentless and non-modal, so otherwise it can land behind whatever you were filming
    // — a window you never asked for, hidden behind the one you were looking at.
    setWindowFlag(Qt::WindowStaysOnTopHint, true);
    resize(760, 560);

    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(12, 12, 12, 12);
    v->setSpacing(8);

    m_video = new QLabel;
    m_video->setAlignment(Qt::AlignCenter);
    m_video->setMinimumSize(320, 200);
    m_video->setStyleSheet(QStringLiteral("background:#101010;border-radius:4px;"));
    v->addWidget(m_video, 1);

    m_slider = new TrimSlider;
    v->addWidget(m_slider);

    auto* row = new QHBoxLayout;
    m_play = new QPushButton;
    m_time = new QLabel(QStringLiteral("0:00 / 0:00"));
    m_saveTrim = new QPushButton;
    m_showFolder = new QPushButton;
    m_close = new QPushButton;
    for (QPushButton* b : {m_play, m_saveTrim, m_showFolder, m_close})
        b->setCursor(Qt::PointingHandCursor);
    row->addWidget(m_play);
    row->addWidget(m_time);
    row->addStretch(1);
    row->addWidget(m_saveTrim);
    row->addWidget(m_showFolder);
    row->addWidget(m_close);
    v->addLayout(row);

    m_hint = new QLabel;
    m_hint->setWordWrap(true);
    m_hint->setStyleSheet(QStringLiteral("color:#666;"));
    applyHintFont();
    v->addWidget(m_hint);

    m_player = platform::createMediaPlayer();
    platform::IMediaPlayer::Callbacks cb;
    cb.onFrame = [this](QImage img, qint64 pos) {
        showFrame(img);
        m_slider->setPosition(pos);
        updateTransport();
    };
    cb.onEnded = [this] { updateTransport(); };

    if (!m_player || !m_player->open(path, cb)) {
        m_video->setText(I18n::instance().t(QStringLiteral("record.preview.cannot_open")));
        m_play->setEnabled(false);
        m_saveTrim->setEnabled(false);
    } else {
        m_slider->setDuration(m_player->durationMs());
        m_player->seek(0);          // decode one frame so the window isn't blank
    }

    connect(m_play, &QPushButton::clicked, this, [this] { togglePlay(); });
    connect(m_saveTrim, &QPushButton::clicked, this, [this] { saveTrimmed(); });
    connect(m_showFolder, &QPushButton::clicked, this, [this] {
        // Select the file in Explorer rather than just opening the folder — and once a trim
        // has been saved, select THAT: saving is otherwise silent, so this button is how you
        // confirm the copy landed. Both files sit in the same folder either way.
        QProcess::startDetached(QStringLiteral("explorer.exe"),
                                QStringList{QStringLiteral("/select,")
                                            + QDir::toNativeSeparators(
                                                  m_lastSaved.isEmpty() ? m_path : m_lastSaved)});
    });
    connect(m_close, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_slider, &TrimSlider::scrubbed, this, [this](qint64 ms) {
        if (m_player) m_player->seek(ms);
        updateTransport();
    });
    connect(m_slider, &TrimSlider::trimChanged, this, [this] { updateTransport(); });

    retranslate();
    updateTransport();
}

RecordPreviewDialog::~RecordPreviewDialog() = default;

void RecordPreviewDialog::resizeEvent(QResizeEvent* e)
{
    QDialog::resizeEvent(e);
    reserveHintHeight();        // a narrower window wraps the hints into more lines
}

void RecordPreviewDialog::changeEvent(QEvent* e)
{
    QDialog::changeEvent(e);
    if (e->type() == QEvent::FontChange)
        applyHintFont();      // the interface font changed while this window was open
}

void RecordPreviewDialog::applyHintFont()
{
    QFont f = font();                          // the dialog's font == the interface font
    f.setPointSizeF(f.pointSizeF() - 1.0);     // one notch under the body text
    m_hint->setFont(f);
    reserveHintHeight();                       // the reservation is measured from these metrics
}

void RecordPreviewDialog::reserveHintHeight()
{
    const QMargins m = layout()->contentsMargins();
    const int w = width() - m.left() - m.right();
    if (w <= 0)
        return;
    const I18n& tr = I18n::instance();
    // Measured with a fixed widest-form range rather than the live one, so the reserved
    // height stays put as the markers move instead of twitching at the 1:00:00 boundary.
    const QString trimmed = tr.t(QStringLiteral("record.preview.trim_hint"))
                                .replace(QStringLiteral("{range}"),
                                         QStringLiteral("0:00:00 – 0:00:00"));
    const QFontMetrics fm = m_hint->fontMetrics();
    int h = 0;
    for (const QString& s : {tr.t(QStringLiteral("record.preview.hint")), trimmed})
        h = qMax(h, fm.boundingRect(QRect(0, 0, w, 0), Qt::TextWordWrap, s).height());
    m_hint->setFixedHeight(h);
}

void RecordPreviewDialog::showFrame(const QImage& img)
{
    m_lastFrame = img;
    if (img.isNull())
        return;
    m_video->setPixmap(QPixmap::fromImage(img).scaled(m_video->size(), Qt::KeepAspectRatio,
                                                      Qt::SmoothTransformation));
}

void RecordPreviewDialog::togglePlay()
{
    if (!m_player)
        return;
    m_player->isPlaying() ? m_player->pause() : m_player->play();
    updateTransport();
}

void RecordPreviewDialog::updateTransport()
{
    const I18n& tr = I18n::instance();
    const qint64 pos = m_player ? m_player->positionMs() : 0;
    const qint64 dur = m_player ? m_player->durationMs() : 0;
    m_time->setText(QStringLiteral("%1 / %2")
                        .arg(recording::timecode(pos), recording::timecode(dur)));
    m_play->setText(tr.t(m_player && m_player->isPlaying() ? QStringLiteral("record.preview.pause")
                                                           : QStringLiteral("record.preview.play")));
    m_saveTrim->setEnabled(m_slider->trimmed());
    m_hint->setText(m_slider->trimmed()
        ? tr.t(QStringLiteral("record.preview.trim_hint"))
              .replace(QStringLiteral("{range}"),
                       QStringLiteral("%1 – %2").arg(recording::timecode(m_slider->trimStart()),
                                                     recording::timecode(m_slider->trimEnd())))
        : tr.t(QStringLiteral("record.preview.hint")));
}

void RecordPreviewDialog::saveTrimmed()
{
    if (!m_slider->trimmed())
        return;
    // A NEW file beside the original — the original is never touched, so a bad trim
    // costs nothing.
    const QFileInfo fi(m_path);
    // The range goes in the name, so several trims of one recording are told apart by what
    // they contain rather than by a counter: "<original>_trimmed_00-02-20_to_00-05-15.mp4".
    const QString stem = fi.completeBaseName()
                         + QStringLiteral("_trimmed_%1_to_%2")
                               .arg(recording::timecodeForFilename(m_slider->trimStart()),
                                    recording::timecodeForFilename(m_slider->trimEnd()));
    QString out = fi.dir().filePath(stem + QLatin1Char('.') + fi.suffix());
    // Trimming the same range twice still needs a counter.
    for (int n = 2; QFile::exists(out); ++n)
        out = fi.dir().filePath(QStringLiteral("%1_%2.%3").arg(stem).arg(n).arg(fi.suffix()));

    if (m_player)
        m_player->pause();
    updateTransport();          // the window stays open, so that pause has to show

    QString err;
    if (!platform::trimRecording(m_path, out, m_slider->trimStart(), m_slider->trimEnd(), &err)) {
        QMessageBox::warning(this, I18n::instance().t(QStringLiteral("record.preview.title")),
                             err.isEmpty()
                                 ? I18n::instance().t(QStringLiteral("record.trim.error.output"))
                                 : err);
        return;
    }
    m_lastSaved = out;          // "Show in folder" now points here, not at the original

    // Deliberately NOT accept(): the window stays open so one recording can yield several
    // ranges. Each save writes its own file (the range is in the name), the markers stay
    // where they are so the next cut starts from this one, and Save stays enabled —
    // re-saving the same range just takes the `_2` counter above.
    // Nor does it open Explorer: with the window staying open, a file manager popping up
    // behind an always-on-top preview after every save is noise. "Show in folder" is there
    // for when it is actually wanted.
}

void RecordPreviewDialog::retranslate()
{
    const I18n& tr = I18n::instance();
    setWindowTitle(tr.t(QStringLiteral("record.preview.title")));
    m_saveTrim->setText(tr.t(QStringLiteral("record.preview.save_trimmed")));
    m_showFolder->setText(tr.t(QStringLiteral("record.preview.show_folder")));
    m_close->setText(tr.t(QStringLiteral("record.preview.close")));
}
