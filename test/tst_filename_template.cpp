#include <QtTest>
#include <QDateTime>

#include "core/FilenameTemplate.h"

// Pure templating logic: "$...$" spans are Qt date/time formats, everything else literal.
class TstFilenameTemplate : public QObject
{
    Q_OBJECT
private slots:
    void literalOnly();
    void datetimeSpan();
    void mixedLiteralAndSpan();
    void emptyTemplate();
    void escapedDollar();
    void danglingDollar();
    void envVar();
    void envVarCaseInsensitive();
    void envVarUnset();
    void escapedPercent();
    void danglingPercent();
    void envAndDatetimeMixed();
    void titleSpecial();
    void titleSpecialLengthLimit();
    void titleSpecialEmptyWithoutSpecials();
    void titleSpecialStripsIllegalChars();
    void titleSpecialSanitizeThenLimit();

private:
    // Fixed instant so assertions are deterministic: 2026-07-02 18:15:37.
    QDateTime when() const
    {
        return QDateTime(QDate(2026, 7, 2), QTime(18, 15, 37));
    }
};

void TstFilenameTemplate::literalOnly()
{
    QCOMPARE(FilenameTemplate::expand(QStringLiteral("screenshot.png"), when()),
             QStringLiteral("screenshot.png"));
}

void TstFilenameTemplate::datetimeSpan()
{
    QCOMPARE(FilenameTemplate::expand(QStringLiteral("$yyyy-MM-dd_HH-mm-ss$"), when()),
             QStringLiteral("2026-07-02_18-15-37"));
}

void TstFilenameTemplate::mixedLiteralAndSpan()
{
    QCOMPARE(FilenameTemplate::expand(QStringLiteral("SnapCap_$yyyy-MM-dd_HH-mm-ss$.png"), when()),
             QStringLiteral("SnapCap_2026-07-02_18-15-37.png"));
}

void TstFilenameTemplate::emptyTemplate()
{
    QCOMPARE(FilenameTemplate::expand(QString(), when()), QString());
}

void TstFilenameTemplate::escapedDollar()
{
    // "$$" collapses to a single literal dollar.
    QCOMPARE(FilenameTemplate::expand(QStringLiteral("cost_$$5.png"), when()),
             QStringLiteral("cost_$5.png"));
}

void TstFilenameTemplate::danglingDollar()
{
    // An unterminated span is emitted verbatim (including the '$').
    QCOMPARE(FilenameTemplate::expand(QStringLiteral("shot_$yyyy"), when()),
             QStringLiteral("shot_$yyyy"));
}

void TstFilenameTemplate::envVar()
{
    // "%name%" expands to the matching environment variable's value.
    qputenv("SNAPCAP_TEST_ENV", "hello");
    QCOMPARE(FilenameTemplate::expand(QStringLiteral("shot_%SNAPCAP_TEST_ENV%.png"), when()),
             QStringLiteral("shot_hello.png"));
    qunsetenv("SNAPCAP_TEST_ENV");
}

void TstFilenameTemplate::envVarCaseInsensitive()
{
    // Lookup is case-insensitive on every platform, so "%os%" == "%OS%".
    qputenv("SNAPCAP_TEST_ENV", "world");
    QCOMPARE(FilenameTemplate::expand(QStringLiteral("%snapcap_test_env%"), when()),
             QStringLiteral("world"));
    qunsetenv("SNAPCAP_TEST_ENV");
}

void TstFilenameTemplate::envVarUnset()
{
    // An unset variable — including the not-yet-wired %title% — resolves to empty.
    qunsetenv("SNAPCAP_DEFINITELY_UNSET");
    QCOMPARE(FilenameTemplate::expand(QStringLiteral("a%SNAPCAP_DEFINITELY_UNSET%b"), when()),
             QStringLiteral("ab"));
    QCOMPARE(FilenameTemplate::expand(QStringLiteral("shot_%title%.png"), when()),
             QStringLiteral("shot_.png"));
}

void TstFilenameTemplate::escapedPercent()
{
    // "%%" collapses to a single literal percent.
    QCOMPARE(FilenameTemplate::expand(QStringLiteral("100%%done.png"), when()),
             QStringLiteral("100%done.png"));
}

void TstFilenameTemplate::danglingPercent()
{
    // An unterminated env span is emitted verbatim (including the '%').
    QCOMPARE(FilenameTemplate::expand(QStringLiteral("shot_%username"), when()),
             QStringLiteral("shot_%username"));
}

void TstFilenameTemplate::envAndDatetimeMixed()
{
    // "%...%" env spans and "$...$" datetime spans coexist in one template.
    qputenv("SNAPCAP_TEST_ENV", "me");
    QCOMPARE(FilenameTemplate::expand(QStringLiteral("%SNAPCAP_TEST_ENV%_$yyyy$.png"), when()),
             QStringLiteral("me_2026.png"));
    qunsetenv("SNAPCAP_TEST_ENV");
}

void TstFilenameTemplate::titleSpecial()
{
    // %title% resolves from the specials map (case-insensitive on the base name).
    const QMap<QString, QString> specials{{QStringLiteral("title"), QStringLiteral("Untitled - Notepad")}};
    QCOMPARE(FilenameTemplate::expand(QStringLiteral("shot_%title%.png"), when(), specials),
             QStringLiteral("shot_Untitled - Notepad.png"));
    QCOMPARE(FilenameTemplate::expand(QStringLiteral("%TITLE%"), when(), specials),
             QStringLiteral("Untitled - Notepad"));
}

void TstFilenameTemplate::titleSpecialLengthLimit()
{
    // %title:N% truncates to N characters; N larger than the value leaves it whole.
    const QMap<QString, QString> specials{{QStringLiteral("title"), QStringLiteral("Untitled - Notepad")}};
    QCOMPARE(FilenameTemplate::expand(QStringLiteral("%title:8%"), when(), specials),
             QStringLiteral("Untitled"));
    QCOMPARE(FilenameTemplate::expand(QStringLiteral("%title:100%"), when(), specials),
             QStringLiteral("Untitled - Notepad"));
}

void TstFilenameTemplate::titleSpecialEmptyWithoutSpecials()
{
    // With no specials passed (or title absent), %title% falls through to env → empty.
    QCOMPARE(FilenameTemplate::expand(QStringLiteral("a%title%b"), when()),
             QStringLiteral("ab"));
    QCOMPARE(FilenameTemplate::expand(QStringLiteral("a%title:5%b"), when(), {}),
             QStringLiteral("ab"));
}

void TstFilenameTemplate::titleSpecialStripsIllegalChars()
{
    // A window title with characters illegal in a filename is sanitized so the save
    // isn't blocked. Reserved set < > : " / \ | ? * → '_'; trailing dots trimmed.
    const QMap<QString, QString> specials{
        {QStringLiteral("title"), QStringLiteral("1:30 - You/Tube.")}};
    QCOMPARE(FilenameTemplate::expand(QStringLiteral("shot_%title%.png"), when(), specials),
             QStringLiteral("shot_1_30 - You_Tube.png"));
}

void TstFilenameTemplate::titleSpecialSanitizeThenLimit()
{
    // %title:N% limits the sanitized value (illegal chars → '_' first, then truncate).
    const QMap<QString, QString> specials{{QStringLiteral("title"), QStringLiteral("a/b/cdef")}};
    QCOMPARE(FilenameTemplate::expand(QStringLiteral("%title:3%"), when(), specials),
             QStringLiteral("a_b"));
}

QTEST_GUILESS_MAIN(TstFilenameTemplate)
#include "tst_filename_template.moc"
