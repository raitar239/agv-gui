#include "appmark.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace agv {

namespace {

QPixmap mark(const QColor &background, int size)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    // The SVG's 256 grid; small sizes drop its margin so the mark stays legible in a tray.
    const qreal margin = size <= 24 ? 4 : 16;
    painter.scale(size / (256.0 - 2 * margin), size / (256.0 - 2 * margin));
    painter.translate(-margin, -margin);

    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawRoundedRect(QRectF(16, 16, 224, 224), 60, 60);

    QPen pen(Qt::white, 18, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    QPainterPath left;
    left.moveTo(128, 198);
    left.lineTo(128, 152);
    left.cubicTo(128, 122, 84, 122, 84, 94);
    QPainterPath right;
    right.moveTo(128, 152);
    right.cubicTo(128, 122, 172, 122, 172, 101);
    painter.drawPath(left);
    painter.drawPath(right);
    pen.setWidthF(15);
    painter.setPen(pen);
    painter.drawEllipse(QPointF(172, 76), 22, 22);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawEllipse(QPointF(84, 76), 20, 20);
    return pixmap;
}

} // namespace

QIcon appMarkIcon(const QColor &background)
{
    QIcon icon;
    for (int size : {16, 22, 24, 32, 48, 64, 128, 256})
        icon.addPixmap(mark(background, size));
    return icon;
}

} // namespace agv
