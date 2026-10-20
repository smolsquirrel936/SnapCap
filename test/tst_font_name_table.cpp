#include <QtTest>
#include <QtEndian>

#include "core/FontNameTable.h"

// Pure sfnt 'name'-table parsing: pick a font's native (CJK) family name. The
// live QRawFont extraction is exercised manually in the app; here we feed
// hand-built tables.
class TstFontNameTable : public QObject
{
    Q_OBJECT
private slots:
    void picksSimplifiedChinese();
    void latinOnlyReturnsEmpty();
    void typographicNameBeatsLegacy();
    void ignoresNonWindowsPlatform();
    void emptyTable();
};

namespace {
struct Rec { quint16 platform, language, nameId; QString text; };

void put16(QByteArray& a, quint16 v)
{
    char b[2];
    qToBigEndian(v, b);
    a.append(b, 2);
}

// Assemble a format-0 'name' table from records (encodingID fixed at 1).
QByteArray buildTable(const QList<Rec>& recs)
{
    const int n = recs.size();
    const quint16 storage = quint16(6 + 12 * n);

    QByteArray strings;
    QList<quint16> offsets, lengths;
    for (const Rec& r : recs) {
        offsets.append(quint16(strings.size()));
        QByteArray s;
        for (QChar c : r.text) put16(s, c.unicode());   // UTF-16BE
        lengths.append(quint16(s.size()));
        strings.append(s);
    }

    QByteArray hdr;
    put16(hdr, 0);         // format
    put16(hdr, quint16(n));
    put16(hdr, storage);
    for (int i = 0; i < n; ++i) {
        put16(hdr, recs[i].platform);
        put16(hdr, 1);     // encodingID
        put16(hdr, recs[i].language);
        put16(hdr, recs[i].nameId);
        put16(hdr, lengths[i]);
        put16(hdr, offsets[i]);
    }
    return hdr + strings;
}
} // namespace

void TstFontNameTable::picksSimplifiedChinese()
{
    const QByteArray t = buildTable({
        {3, 0x0409, 1, QStringLiteral("Microsoft YaHei")},   // en-US
        {3, 0x0804, 1, QStringLiteral("微软雅黑")},           // zh-CN
    });
    QCOMPARE(core::parseNativeFamilyName(t), QStringLiteral("微软雅黑"));
}

void TstFontNameTable::latinOnlyReturnsEmpty()
{
    const QByteArray t = buildTable({
        {3, 0x0409, 1, QStringLiteral("Segoe UI")},
    });
    QVERIFY(core::parseNativeFamilyName(t).isEmpty());
}

void TstFontNameTable::typographicNameBeatsLegacy()
{
    // nameID 16 (typographic family) must win over nameID 1 (legacy family).
    const QByteArray t = buildTable({
        {3, 0x0804, 1,  QStringLiteral("宋体 Legacy")},
        {3, 0x0804, 16, QStringLiteral("宋体")},
    });
    QCOMPARE(core::parseNativeFamilyName(t), QStringLiteral("宋体"));
}

void TstFontNameTable::ignoresNonWindowsPlatform()
{
    // Only Windows platform (3) records are UTF-16BE; a Mac (1) record is skipped.
    const QByteArray t = buildTable({
        {1, 0x0804, 1, QStringLiteral("bogus")},
    });
    QVERIFY(core::parseNativeFamilyName(t).isEmpty());
}

void TstFontNameTable::emptyTable()
{
    QVERIFY(core::parseNativeFamilyName(QByteArray()).isEmpty());
    QVERIFY(core::parseNativeFamilyName(QByteArray("\0\0", 2)).isEmpty());
}

QTEST_APPLESS_MAIN(TstFontNameTable)
#include "tst_font_name_table.moc"
