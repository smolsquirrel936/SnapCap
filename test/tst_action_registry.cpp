#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QRegularExpression>

#include "core/action/ActionRegistry.h"

// Stage 1: the command bus — handler dispatch + context-aware keymap.
class TstActionRegistry : public QObject
{
    Q_OBJECT
private slots:
    void dispatchCallsHandler();
    void dispatchUnboundReturnsFalse();
    void contextDisambiguatesSameKey();
    void loadsDefaultsFromYaml();
    void realKeymapNamesAllResolve();
};

void TstActionRegistry::dispatchCallsHandler()
{
    ActionRegistry r;
    int calls = 0;
    r.registerAction(ActionId::Snip, [&] { ++calls; });
    QVERIFY(r.dispatch(ActionId::Snip));
    QCOMPARE(calls, 1);
}

void TstActionRegistry::dispatchUnboundReturnsFalse()
{
    ActionRegistry r;
    QVERIFY(!r.dispatch(ActionId::Paste));      // no handler registered
}

void TstActionRegistry::contextDisambiguatesSameKey()
{
    ActionRegistry r;
    r.bindKey(ActionId::Snip_Fullscreen, QStringLiteral("Ctrl+A"), KeyContext::Snip);
    r.bindKey(ActionId::Pin_SelectAll,   QStringLiteral("Ctrl+A"), KeyContext::Pin);

    const auto inSnip = r.actionFor(QStringLiteral("ctrl+a"), KeyContext::Snip);
    const auto inPin  = r.actionFor(QStringLiteral("Ctrl+A"), KeyContext::Pin);
    QVERIFY(inSnip.has_value());
    QCOMPARE(*inSnip, ActionId::Snip_Fullscreen);
    QVERIFY(inPin.has_value());
    QCOMPARE(*inPin, ActionId::Pin_SelectAll);

    QVERIFY(!r.actionFor(QStringLiteral("Ctrl+B"), KeyContext::Snip).has_value());
}

void TstActionRegistry::loadsDefaultsFromYaml()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("keymap.yaml"));
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("global:\n  snip: \"F1\"\n  paste: \"F3\"\n  clickThrough: \"\"\n");
    }
    ActionRegistry r;
    QVERIFY(r.loadDefaults(path));
    QCOMPARE(r.keyFor(ActionId::Snip), QStringLiteral("F1"));
    QCOMPARE(r.keyFor(ActionId::Paste), QStringLiteral("F3"));
    QCOMPARE(r.keyFor(ActionId::ToggleClickThrough), QString());   // empty -> unbound
    const auto a = r.actionFor(QStringLiteral("F1"), KeyContext::Global);
    QVERIFY(a.has_value() && *a == ActionId::Snip);
}

// Regression: every action name in the REAL res/keymap.default.yaml must resolve
// through actionIdFromName(). loadDefaults() only warns on an unknown name, and
// the same name map is what PreferencesDialog uses to bindKey() an edited hotkey
// — so adding an ActionId + a keymap row but forgetting the map entry leaves the
// action silently unbindable: the sequence saves to config.ini and never
// registers. SnipActiveWindow shipped broken exactly this way once.
void TstActionRegistry::realKeymapNamesAllResolve()
{
    const QString path = QStringLiteral(SNAPCAP_SOURCE_DIR "/res/keymap.default.yaml");
    QFile f(path);
    QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(path));
    const QString text = QString::fromUtf8(f.readAll());

    // Action rows are the indented "  name: <value>" lines; group headers sit at
    // column 0 and are matched by loadDefaults' own context table instead.
    static const QRegularExpression row(QStringLiteral(R"(^[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*:)"),
                                        QRegularExpression::MultilineOption);
    int checked = 0;
    for (auto it = row.globalMatch(text); it.hasNext(); ) {
        const QString name = it.next().captured(1);
        QVERIFY2(ActionRegistry::actionIdFromName(name).has_value(),
                 qPrintable(QStringLiteral("keymap action '%1' has no actionIdFromName() entry")
                                .arg(name)));
        ++checked;
    }
    QVERIFY(checked > 0);        // guard against the regex silently matching nothing
}

QTEST_APPLESS_MAIN(TstActionRegistry)
#include "tst_action_registry.moc"
