#include "iconprovider.h"

#include <QFileInfo>
#include <QIcon>
#include <QUrl>

namespace agv {

AppIconProvider::AppIconProvider()
    : QQuickImageProvider(QQuickImageProvider::Pixmap)
{
}

QPixmap AppIconProvider::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize)
{
    const int side = requestedSize.width() > 0 ? requestedSize.width() : 48;
    const QString name = QUrl::fromPercentEncoding(id.toUtf8());

    QIcon icon;
    if (name.startsWith(QLatin1Char('/'))) {
        icon = QIcon(name);
    } else if (!name.isEmpty()) {
        icon = QIcon::fromTheme(name);
        if (icon.isNull()) {
            // Many apps still drop their icon into /usr/share/pixmaps only.
            for (const char *extension : {".png", ".svg", ".xpm"}) {
                const QString path = QStringLiteral("/usr/share/pixmaps/") + name + QLatin1String(extension);
                if (QFileInfo::exists(path)) {
                    icon = QIcon(path);
                    break;
                }
            }
        }
    }
    if (icon.isNull())
        icon = QIcon::fromTheme(QStringLiteral("application-x-executable"));

    const QPixmap pixmap = icon.pixmap(side, side);
    if (size)
        *size = pixmap.size();
    return pixmap;
}

} // namespace agv
