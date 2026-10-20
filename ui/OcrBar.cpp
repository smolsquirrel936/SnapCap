#include "ui/OcrBar.h"

#include "core/i18n/I18n.h"
#include "ui/PrefsStyle.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QMap>
#include <QPushButton>

OcrBar::OcrBar(QWidget* parent) : QWidget(parent)
{
    m_accent = PrefsStyle::accentColor();
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(QStringLiteral("ocrBar"));

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(6, 4, 6, 4);
    row->setSpacing(4);

    auto makeBtn = [this](bool checkable) {
        auto* b = new QPushButton(this);
        b->setCheckable(checkable);
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };

    m_selectAll = makeBtn(false);
    m_copy      = makeBtn(false);
    m_lineBreak = makeBtn(true);
    m_lineBreak->setChecked(true);         // keep line breaks by default
    m_lang      = new QComboBox(this);
    m_lang->setCursor(Qt::PointingHandCursor);

    row->addWidget(m_selectAll);
    row->addWidget(m_copy);
    row->addWidget(m_lineBreak);
    row->addWidget(m_lang);

    connect(m_selectAll, &QPushButton::clicked, this, &OcrBar::selectAllRequested);
    connect(m_copy,      &QPushButton::clicked, this, &OcrBar::copyRequested);
    connect(m_lineBreak, &QPushButton::toggled, this, &OcrBar::keepLineBreaksChanged);
    connect(m_lang, &QComboBox::currentIndexChanged, this, [this](int) {
        emit languageChanged(currentLanguage());
    });

    setStyleSheet(barQss());
    retranslate();
}

QString OcrBar::barQss() const
{
    QString q = QStringLiteral(
        "QWidget#ocrBar{background:#f2f2f2;border:1px solid #c4c4c4;border-radius:6px;}"
        "QPushButton{background:transparent;border:1px solid transparent;border-radius:4px;"
        "padding:3px 8px;color:#222;}"
        "QPushButton:hover{background:#e3e3e3;}"
        "QPushButton:checked{background:#d4d4d4;border:1px solid #2d7dff;}"
        "QComboBox{color:#222;background:#fff;border:1px solid #c4c4c4;border-radius:4px;"
        "padding:2px 6px;}");
    q.replace(QStringLiteral("#2d7dff"),
              (m_accent.isValid() ? m_accent : QColor(0x2d, 0x7d, 0xff)).name(),
              Qt::CaseInsensitive);
    return q;
}

void OcrBar::setAccent(const QColor& c)
{
    m_accent = c;
    setStyleSheet(barQss());
}

QString OcrBar::displayName(const QString& code)
{
    const QString key = QStringLiteral("ocr.lang.") + code;
    const QString name = I18n::instance().t(key);
    return name == key ? code : name;   // t() returns the raw key when it has no translation
}

void OcrBar::setLanguages(const QStringList& codes, const QString& current)
{
    QSignalBlocker block(m_lang);
    m_lang->clear();
    for (const QString& code : codes)
        m_lang->addItem(displayName(code), code);   // show the name, keep the code as data
    const int idx = m_lang->findData(current);
    if (idx >= 0)
        m_lang->setCurrentIndex(idx);
}

QString OcrBar::currentLanguage() const
{
    return m_lang->currentData().toString();
}

bool OcrBar::keepLineBreaks() const
{
    return m_lineBreak->isChecked();
}

QString OcrBar::defaultLanguage(const QStringList& installed)
{
    if (installed.isEmpty())
        return QStringLiteral("eng");
    // UI locale → Tesseract code; prefer it if installed, else English, else whatever exists.
    static const QMap<QString, QString> kMap = {
        {QStringLiteral("en"), QStringLiteral("eng")},
        {QStringLiteral("zh_cn"), QStringLiteral("chi_sim")},
        {QStringLiteral("zh_tw"), QStringLiteral("chi_tra")},
        {QStringLiteral("ja_jp"), QStringLiteral("jpn")},
        {QStringLiteral("ko"), QStringLiteral("kor")},
        {QStringLiteral("de_de"), QStringLiteral("deu")},
        {QStringLiteral("fr_fr"), QStringLiteral("fra")},
        {QStringLiteral("es_mx"), QStringLiteral("spa")},
        {QStringLiteral("pt_br"), QStringLiteral("por")},
        {QStringLiteral("pt_pt"), QStringLiteral("por")},
        {QStringLiteral("nl_nl"), QStringLiteral("nld")},
        {QStringLiteral("ru"), QStringLiteral("rus")},
    };
    const QString desired = kMap.value(I18n::instance().locale());
    if (!desired.isEmpty() && installed.contains(desired))
        return desired;
    if (installed.contains(QStringLiteral("eng")))
        return QStringLiteral("eng");
    return installed.first();
}

void OcrBar::retranslate()
{
    I18n& tr = I18n::instance();
    m_selectAll->setText(tr.t(QStringLiteral("ocr.bar.selectall")));
    m_copy->setText(tr.t(QStringLiteral("ocr.bar.copy")));
    m_lineBreak->setText(tr.t(QStringLiteral("ocr.bar.linebreak")));
    m_lineBreak->setToolTip(tr.t(QStringLiteral("ocr.bar.linebreak_tip")));
    m_lang->setToolTip(tr.t(QStringLiteral("ocr.bar.language")));
}
