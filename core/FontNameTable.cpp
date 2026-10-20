#include "core/FontNameTable.h"

#include <QtEndian>

namespace core {

QString parseNativeFamilyName(const QByteArray& nameTable)
{
    // 'name' table layout (format 0): u16 format, u16 count, u16 storageOffset,
    // then `count` 12-byte records {platformID, encodingID, languageID, nameID,
    // length, offset}, then the string storage (offset is relative to storage).
    if (nameTable.size() < 6)
        return {};
    const uchar* d = reinterpret_cast<const uchar*>(nameTable.constData());
    const int size = nameTable.size();
    const quint16 count   = qFromBigEndian<quint16>(d + 2);
    const quint16 storage = qFromBigEndian<quint16>(d + 4);

    // Preferred native language IDs, best first: Simplified Chinese (CN, SG),
    // Traditional Chinese (TW, HK, MO), Japanese, Korean.
    static const quint16 kLangs[] = {0x0804, 0x1004, 0x0404, 0x0c04, 0x1404, 0x0411, 0x0412};
    constexpr int kLangCount = int(sizeof(kLangs) / sizeof(kLangs[0]));

    QString best;
    int bestNameRank = 99, bestLangRank = 99;

    for (int i = 0; i < count; ++i) {
        const uchar* r = d + 6 + i * 12;
        if (r + 12 > d + size)
            break;
        const quint16 platformId = qFromBigEndian<quint16>(r + 0);
        const quint16 languageId = qFromBigEndian<quint16>(r + 4);
        const quint16 nameId     = qFromBigEndian<quint16>(r + 6);
        const quint16 length     = qFromBigEndian<quint16>(r + 8);
        const quint16 offset     = qFromBigEndian<quint16>(r + 10);

        if (platformId != 3)             // Windows platform: strings are UTF-16BE
            continue;
        if (nameId != 1 && nameId != 16) // 1 = family, 16 = typographic family
            continue;

        int langRank = -1;
        for (int k = 0; k < kLangCount; ++k)
            if (kLangs[k] == languageId) { langRank = k; break; }
        if (langRank < 0)
            continue;

        // Typographic family (16) beats legacy family (1); within the same nameID
        // the earlier (more-preferred) language wins.
        const int nameRank = (nameId == 16) ? 0 : 1;
        if (nameRank > bestNameRank)
            continue;
        if (nameRank == bestNameRank && langRank >= bestLangRank)
            continue;

        const int base = storage + offset;
        if (base < 0 || base + length > size)
            continue;

        QString s;
        for (int b = 0; b + 1 < length; b += 2)
            s.append(QChar(qFromBigEndian<quint16>(d + base + b)));
        if (s.isEmpty())
            continue;

        best = s;
        bestNameRank = nameRank;
        bestLangRank = langRank;
    }
    return best;
}

} // namespace core
