#pragma once
#include <cmath>

namespace dt::page {
struct Insets { double left = 0, top = 0, right = 0, bottom = 0; };
struct Corners { double tl = 0, tr = 0, br = 0, bl = 0; };
inline bool Small(double value) { return std::isfinite(value) && value > 0 && value <= 12; }
inline double Gap(double value) { return Small(value) ? std::round(value * 2) / 2 : 0; }
inline Insets Strip(Insets value) {
    return {Small(value.left) ? 0 : value.left, Small(value.top) ? 0 : value.top,
            Small(value.right) ? 0 : value.right, Small(value.bottom) ? 0 : value.bottom};
}
inline Corners Square(Corners value, bool left, bool right) {
    return {left ? 0 : value.tl, right ? 0 : value.tr, right ? 0 : value.br, left ? 0 : value.bl};
}
inline bool HasRadius(Corners value) { return value.tl > 0 || value.tr > 0 || value.br > 0 || value.bl > 0; }
}
