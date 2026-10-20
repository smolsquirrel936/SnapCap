#include "ui/RecordBar.h"

#include "core/i18n/I18n.h"
#include "core/recording/RecordingSpec.h"
#include "ui/PrefsStyle.h"

#include <QCoreApplication>
#include <QDir>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QWheelEvent>

namespace {
constexpr int kMaxDelay = 60 * 60 - 1;      // 59:59; the field only has room for MM:SS

// <exe>/icons/<name>.svg -- the same folder and format PaintBar::loadIcon reads.
QIcon barIcon(const char* name)
{
    return QIcon(QDir(QCoreApplication::applicationDirPath())
                     .filePath(QStringLiteral("icons/%1.svg").arg(QLatin1String(name))));
}
}

// ---------------------------------------------------------------- RecordTimerField

RecordTimerField::RecordTimerField(QWidget* parent) : QWidget(parent)
{
    m_accent = PrefsStyle::accentColor();
    setFocusPolicy(Qt::ClickFocus);
    setCursor(Qt::IBeamCursor);
    showDelay();
}

QSize RecordTimerField::sizeHint() const
{
    QFont f = font();
    f.setBold(true);
    // Sized for the widest thing it ever shows (an hour-long elapsed clock), so the
    // bar doesn't resize as the timer ticks past 59:59.
    return QSize(QFontMetrics(f).horizontalAdvance(QStringLiteral("-0:00:00")) + 16, 26);
}

void RecordTimerField::setDelaySeconds(int s)
{
    m_delay = qBound(0, s, kMaxDelay);
    if (m_editable)
        showDelay();
}

void RecordTimerField::setEditable(bool on)
{
    m_editable = on;
    m_typing = false;
    m_typed.clear();
    setCursor(on ? Qt::IBeamCursor : Qt::ArrowCursor);
    update();
}

void RecordTimerField::showDelay()
{
    m_text = QStringLiteral("-") + recording::timecode(qint64(m_delay) * 1000);
    update();
}

void RecordTimerField::showCountdown(int secondsLeft)
{
    m_text = QStringLiteral("-") + recording::timecode(qint64(qMax(0, secondsLeft)) * 1000);
    update();
}

void RecordTimerField::showMilliseconds(qint64 ms)
{
    m_text = recording::timecode(ms);
    update();
}

void RecordTimerField::wheelEvent(QWheelEvent* e)
{
    if (!m_editable) { e->ignore(); return; }
    const int steps = e->angleDelta().y() / 120;
    if (!steps) { e->ignore(); return; }
    setDelaySeconds(m_delay + steps);
    emit delayChanged(m_delay);
    e->accept();
}

void RecordTimerField::mousePressEvent(QMouseEvent* e)
{
    if (!m_editable) { e->ignore(); return; }
    m_typing = true;                  // click to type: the first digit replaces the value
    m_typed.clear();
    setFocus(Qt::MouseFocusReason);
    update();
}

void RecordTimerField::keyPressEvent(QKeyEvent* e)
{
    if (!m_editable || !m_typing) { QWidget::keyPressEvent(e); return; }

    if (e->key() == Qt::Key_Escape) { m_typing = false; m_typed.clear(); showDelay(); return; }
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) { commitTyped(); return; }
    if (e->key() == Qt::Key_Backspace) {
        m_typed.chop(1);
        update();
        return;
    }
    const QString t = e->text();
    if (t.size() == 1 && t.at(0).isDigit() && m_typed.size() < 4) {
        m_typed += t;                 // typed as MMSS, right-aligned (e.g. "5" = 5s, "130" = 1:30)
        update();
        return;
    }
    QWidget::keyPressEvent(e);
}

void RecordTimerField::focusOutEvent(QFocusEvent*)
{
    if (m_typing)
        commitTyped();
}

void RecordTimerField::commitTyped()
{
    if (!m_typed.isEmpty()) {
        const int v = m_typed.toInt();
        // Right-aligned MMSS: the last two digits are seconds, the rest minutes.
        const int secs = (m_typed.size() <= 2) ? v : (v / 100) * 60 + (v % 100);
        setDelaySeconds(secs);
        emit delayChanged(m_delay);
    }
    m_typing = false;
    m_typed.clear();
    showDelay();
}

void RecordTimerField::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const bool active = m_typing && m_editable;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(active ? 0xff : 0xf7, active ? 0xff : 0xf7, active ? 0xff : 0xf7));
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 4, 4);
    p.setBrush(Qt::NoBrush);
    p.setPen(active ? m_accent : QColor(0xc4, 0xc4, 0xc4));
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 4, 4);

    QFont f = font();
    f.setBold(true);
    p.setFont(f);
    // While typing, show what has been keyed so far rather than the committed value.
    const QString text = active ? (QStringLiteral("-") + (m_typed.isEmpty() ? QStringLiteral("_")
                                                                            : m_typed))
                                : m_text;
    p.setPen(m_editable ? QColor(0x22, 0x22, 0x22) : QColor(0xc0, 0x39, 0x2b));
    p.drawText(rect(), Qt::AlignCenter, text);
}

// ---------------------------------------------------------------------- RecordBar

RecordBar::RecordBar(QWidget* parent) : QWidget(parent)
{
    m_accent = PrefsStyle::accentColor();
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(QStringLiteral("recordBar"));

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(6, 4, 6, 4);
    row->setSpacing(4);

    // Icon-only buttons at the paint bar's metrics (20 px icon in a 30 px button). The
    // labels they used to carry are now their tooltips and accessible names.
    m_record = new QPushButton(this);
    m_record->setCursor(Qt::PointingHandCursor);
    m_record->setObjectName(QStringLiteral("recordGo"));
    m_record->setIconSize(QSize(20, 20));
    m_record->setFixedSize(30, 30);
    m_pause = new QPushButton(this);
    m_pause->setCursor(Qt::PointingHandCursor);
    m_pause->setIconSize(QSize(20, 20));
    m_pause->setFixedSize(30, 30);
    m_timer = new RecordTimerField(this);

    row->addWidget(m_record);
    row->addWidget(m_pause);
    row->addWidget(m_timer);

    connect(m_record, &QPushButton::clicked, this, [this] {
        if (m_state == State::Idle)
            emit startRequested();
        else
            emit stopRequested();
    });
    connect(m_pause, &QPushButton::clicked, this, [this] {
        if (m_state == State::Recording)      emit pauseToggled(true);
        else if (m_state == State::Paused)    emit pauseToggled(false);
    });
    connect(m_timer, &RecordTimerField::delayChanged, this, &RecordBar::delayChanged);

    setStyleSheet(barQss());
    refreshButtons();
    retranslate();
}

QString RecordBar::barQss() const
{
    QString q = QStringLiteral(
        "QWidget#recordBar{background:#f2f2f2;border:1px solid #c4c4c4;border-radius:6px;}"
        "QPushButton{background:transparent;border:1px solid transparent;border-radius:4px;"
        "padding:2px;}"
        "QPushButton:hover{background:#e3e3e3;}"
        // The primary action reads as the record/stop control, not another grey button.
        // (A disabled Pause needs no rule: Qt greys a disabled button's icon itself.)
        "QPushButton#recordGo:hover{background:#f5dcd8;}");
    return q;
}

void RecordBar::setAccent(const QColor& c)
{
    m_accent = c;
    setStyleSheet(barQss());
}

void RecordBar::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    m_timer->setEditable(s == State::Idle);
    if (s == State::Idle)
        m_timer->showDelay();
    refreshButtons();
    retranslate();
}

void RecordBar::refreshButtons()
{
    // Pause is meaningless before the first frame — during the countdown there is
    // nothing to pause, so it stays disabled until recording actually runs.
    m_pause->setEnabled(m_state == State::Recording || m_state == State::Paused);
    adjustSize();
}

void RecordBar::retranslate()
{
    // Icons are set here too because, like the text they replaced, they follow the
    // state: start <-> stop, pause <-> resume (setState() calls this on every change).
    const I18n& tr = I18n::instance();
    const bool idle = m_state == State::Idle;
    const bool paused = m_state == State::Paused;
    m_record->setIcon(barIcon(idle ? "record_start" : "record_stop"));
    m_record->setAccessibleName(idle ? tr.t(QStringLiteral("record.bar.record"))
                                     : tr.t(QStringLiteral("record.bar.stop")));
    m_record->setToolTip(idle ? tr.t(QStringLiteral("record.bar.record_tip"))
                              : tr.t(QStringLiteral("record.bar.stop_tip")));
    const QString pauseLabel = paused ? tr.t(QStringLiteral("record.bar.resume"))
                                      : tr.t(QStringLiteral("record.bar.pause"));
    m_pause->setIcon(barIcon(paused ? "record_resume" : "record_pause"));
    m_pause->setAccessibleName(pauseLabel);
    m_pause->setToolTip(pauseLabel);
    m_timer->setToolTip(tr.t(QStringLiteral("record.bar.timer_tip")));
    adjustSize();
}

int  RecordBar::delaySeconds() const           { return m_timer->delaySeconds(); }
void RecordBar::setDelaySeconds(int s)         { m_timer->setDelaySeconds(s); }
void RecordBar::setElapsed(qint64 ms)          { m_timer->showMilliseconds(ms); }
void RecordBar::setCountdown(int secondsLeft)  { m_timer->showCountdown(secondsLeft); }
