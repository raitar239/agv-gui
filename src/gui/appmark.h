#pragma once

#include <QColor>
#include <QIcon>

namespace agv {

// AGV's mark (data/icons/agv-gui.svg) drawn in code, for the tray and as the window icon when
// the icon theme does not have it: a route splitting into one that goes straight out (dot) and
// one into the tunnel (ring), white on a rounded square of the given color.
QIcon appMarkIcon(const QColor &background);

} // namespace agv
