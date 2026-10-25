#pragma once

#include <QQuickImageProvider>

namespace agv {

// "image://appicon/<name or absolute path>" for QML: application icons from the icon theme,
// with hicolor and /usr/share/pixmaps as fallbacks.
class AppIconProvider : public QQuickImageProvider
{
public:
    AppIconProvider();

    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;
};

} // namespace agv
