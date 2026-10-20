#include "ui/CustomSnipDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>

#include "core/Settings.h"
#include "core/i18n/I18n.h"
#include "ui/PrefsStyle.h"

CustomSnipDialog::CustomSnipDialog(Settings* settings, QWidget* parent)
    : QDialog(parent), m_settings(settings)
{
    setWindowTitle(TR("prefs.customsnip.title"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 18);
    root->setSpacing(14);

    auto* intro = new QLabel(TR("prefs.customsnip.intro"));
    intro->setWordWrap(true);
    root->addWidget(intro);

    // Capture px on the virtual desktop, so X/Y go negative on a monitor left of /
    // above the primary one.
    auto makeCoord = [](int min) {
        auto* s = new QSpinBox;
        s->setRange(min, 32767);
        s->setSuffix(TR("prefs.customsnip.px"));
        return s;
    };
    m_x      = makeCoord(-32768);
    m_y      = makeCoord(-32768);
    m_width  = makeCoord(1);
    m_height = makeCoord(1);

    m_delay = new QDoubleSpinBox;
    m_delay->setRange(0.0, 60.0);
    m_delay->setDecimals(1);
    m_delay->setSingleStep(0.5);
    m_delay->setSuffix(TR("prefs.customsnip.seconds"));

    m_x->setValue(m_settings->value(QStringLiteral("snip/customSnip/x"), 0).toInt());
    m_y->setValue(m_settings->value(QStringLiteral("snip/customSnip/y"), 0).toInt());
    m_width->setValue(m_settings->value(QStringLiteral("snip/customSnip/width"), 800).toInt());
    m_height->setValue(m_settings->value(QStringLiteral("snip/customSnip/height"), 600).toInt());
    m_delay->setValue(m_settings->value(QStringLiteral("snip/customSnip/delay"), 0.0).toDouble());

    auto* form = new QFormLayout;
    form->setSpacing(10);
    form->addRow(TR("prefs.customsnip.x"), m_x);
    form->addRow(TR("prefs.customsnip.y"), m_y);
    form->addRow(TR("prefs.customsnip.width"), m_width);
    form->addRow(TR("prefs.customsnip.height"), m_height);
    form->addRow(TR("prefs.customsnip.delay"), m_delay);
    root->addLayout(form);

    auto* delayHint = new QLabel(TR("prefs.customsnip.delay_hint"));
    delayHint->setWordWrap(true);
    delayHint->setEnabled(false);          // muted: it is a hint, not a control
    root->addWidget(delayHint);

    auto* bar = new QHBoxLayout;
    bar->addStretch();
    auto* okBtn = new QPushButton(TR("prefs.customsnip.ok"));
    auto* cancelBtn = new QPushButton(TR("prefs.customsnip.cancel"));
    okBtn->setDefault(true);
    connect(okBtn, &QPushButton::clicked, this, [this] { save(); accept(); });
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    bar->addWidget(okBtn);
    bar->addWidget(cancelBtn);
    root->addLayout(bar);

    setStyleSheet(PrefsStyle::load());
}

void CustomSnipDialog::save()
{
    m_settings->setValue(QStringLiteral("snip/customSnip/x"), m_x->value());
    m_settings->setValue(QStringLiteral("snip/customSnip/y"), m_y->value());
    m_settings->setValue(QStringLiteral("snip/customSnip/width"), m_width->value());
    m_settings->setValue(QStringLiteral("snip/customSnip/height"), m_height->value());
    m_settings->setValue(QStringLiteral("snip/customSnip/delay"), m_delay->value());
}
