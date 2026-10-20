#include "ui/SaveFeedback.h"

#include <QDir>
#include <QImage>
#include <QMessageBox>
#include <QSize>

#include "core/ImageExport.h"
#include "core/i18n/I18n.h"

namespace SaveFeedback {

bool confirmIcoDownscale(QWidget* parent, const QImage& img, const QString& path)
{
    if (!ImageExport::icoWouldDownscale(img, path))
        return true;

    // Mirror what Qt's ICO writer does, so the numbers we quote are the real ones.
    // expandedTo() matters for extreme aspect ratios: a 1000x3 strip scales to a
    // truncated 256x0 here, while Qt clamps to 256x1 — quoting "256x0" would be a lie.
    const QSize shrunk = img.size()
                             .scaled(ImageExport::kIcoMaxSide, ImageExport::kIcoMaxSide,
                                     Qt::KeepAspectRatio)
                             .expandedTo(QSize(1, 1));
    const QString text = I18n::instance().t(
        QStringLiteral("error.ico_downscale"),
        { { QStringLiteral("max"), ImageExport::kIcoMaxSide },
          { QStringLiteral("w"),   img.width() },
          { QStringLiteral("h"),   img.height() },
          { QStringLiteral("nw"),  shrunk.width() },
          { QStringLiteral("nh"),  shrunk.height() } });

    return QMessageBox::question(parent, TR("error.ico_downscale_title"), text,
                                 QMessageBox::Save | QMessageBox::Cancel,
                                 QMessageBox::Save) == QMessageBox::Save;
}

void warnSaveFailed(QWidget* parent, const QString& path, const QString& reason)
{
    QMessageBox::warning(
        parent, TR("error.save_failed_title"),
        I18n::instance().t(QStringLiteral("error.save_failed"),
                           { { QStringLiteral("path"), QDir::toNativeSeparators(path) },
                             { QStringLiteral("reason"), reason } }));
}

} // namespace SaveFeedback
