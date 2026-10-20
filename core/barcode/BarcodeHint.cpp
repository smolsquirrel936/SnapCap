#include "core/barcode/BarcodeHint.h"

#include <QRegularExpression>
#include <QUrl>

namespace BarcodeHint {

namespace {

const QString kEllipsis = QStringLiteral("…");   // "…"

// The payload minus a leading http:// or https:// (case-insensitive). Anything
// else is returned untouched.
QString withoutScheme(const QString& s)
{
    for (const QString& scheme : {QStringLiteral("https://"), QStringLiteral("http://")})
        if (s.startsWith(scheme, Qt::CaseInsensitive))
            return s.mid(scheme.size());
    return s;
}

// Longest prefix of `s` that fits, with the ellipsis appended. Empty if even one
// character + the ellipsis is too wide.
QString clipTail(const QString& s, int maxWidthPx, const Measure& measure)
{
    for (int n = s.size() - 1; n >= 1; --n) {
        const QString cand = s.left(n) + kEllipsis;
        if (measure(cand) <= maxWidthPx)
            return cand;
    }
    return QString();
}

} // namespace

bool isOpenableUrl(const QString& payload)
{
    const QString s = payload.trimmed();
    if (!s.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
        && !s.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive))
        return false;
    const QUrl u(s, QUrl::StrictMode);
    return u.isValid() && !u.host().isEmpty();
}

QString singleLine(const QString& payload)
{
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    return payload.simplified().replace(ws, QStringLiteral(" "));
}

QString elide(const QString& payload, int maxWidthPx, const Measure& measure)
{
    const QString full = singleLine(payload);
    if (full.isEmpty() || maxWidthPx <= 0)
        return QString();

    // 1. Whole thing fits.
    if (measure(full) <= maxWidthPx)
        return full;

    // 2. Drop the scheme.
    const QString bare = withoutScheme(full);
    if (measure(bare) <= maxWidthPx)
        return bare;

    // 3. host + "/…" + the longest tail that still fits. Only for payloads that
    //    actually have a host to preserve; `slash` is where the path starts.
    const int slash = bare.indexOf(QLatin1Char('/'));
    if (isOpenableUrl(full) && slash > 0) {
        const QString head = bare.left(slash) + QStringLiteral("/") + kEllipsis;
        const QString path = bare.mid(slash + 1);
        if (measure(head) <= maxWidthPx) {
            // Grow the tail from the right for as long as the whole thing fits.
            QString best = head;
            for (int n = 1; n <= path.size(); ++n) {
                const QString cand = head + path.right(n);
                if (measure(cand) > maxWidthPx)
                    break;
                best = cand;
            }
            return best;
        }
    }

    // 4. Budget too tight for host+tail (or a non-URL payload): plain tail clip.
    return clipTail(bare, maxWidthPx, measure);
}

} // namespace BarcodeHint
