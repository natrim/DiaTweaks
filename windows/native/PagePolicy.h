#pragma once
#include <cmath>
#include <span>

namespace dt::page {
struct Insets { double left = 0, top = 0, right = 0, bottom = 0; };
struct Box { double x, y, width, height; };
// Full page chrome may include the revealed toolbar above the web surface.
// Reject ancestors that widen into the sidebar or the whole window.
inline bool FrameAncestor(Box page, Box outer) {
    double left = page.x - outer.x, top = page.y - outer.y;
    double right = outer.x + outer.width - page.x - page.width;
    double bottom = outer.y + outer.height - page.y - page.height;
    return left >= -1 && left <= 12 && right >= -1 && right <= 12 &&
           top >= -1 && top <= 128 && bottom >= -1 && bottom <= 12;
}
struct Corners { double tl = 0, tr = 0, br = 0, bl = 0; };
inline bool Small(double value) { return std::isfinite(value) && value > 0 && value <= 12; }
inline double Gap(double value) { return Small(value) ? std::round(value * 2) / 2 : 0; }
inline Insets Strip(Insets value) {
    return {Small(value.left) ? 0 : value.left, Small(value.top) ? 0 : value.top,
            Small(value.right) ? 0 : value.right, Small(value.bottom) ? 0 : value.bottom};
}
inline size_t FrameCount(Box page, std::span<Box const> ancestors) {
    size_t count = 0;
    for (auto const& candidate : ancestors) {
        if (!FrameAncestor(page, candidate)) break;
        ++count;
    }
    return count;
}
inline Insets FrameMargin(Insets value, bool edge) { return edge ? Strip(value) : value; }
inline Corners Square(Corners value, bool left, bool right) {
    return {left ? 0 : value.tl, right ? 0 : value.tr, right ? 0 : value.br, left ? 0 : value.bl};
}
inline bool HasRadius(Corners value) { return value.tl > 0 || value.tr > 0 || value.br > 0 || value.bl > 0; }
}
