#pragma once
#include <cstdint>

namespace dt {
struct HoverSample { bool ready = false, active = false, pointer = false, focus = false; };
struct HoverState {
    bool revealed = false;
    std::uint64_t hideAt = 0;
    void Reset() { revealed = false; hideAt = 0; }
    bool Update(HoverSample sample, std::uint64_t now) {
        if (!sample.ready || !sample.active) { Reset(); return revealed; }
        if (sample.pointer || (revealed && sample.focus)) { revealed = true; hideAt = 0; }
        else if (revealed) {
            if (!hideAt) hideAt = now + 250;
            if (now >= hideAt) Reset();
        }
        return revealed;
    }
};
inline bool InHoverBand(double x, double y, double left, double top, double width,
                        double paneHeight, double toolbarHeight, bool revealed) {
    double height = revealed ? toolbarHeight + 8 : 8;
    if (height > paneHeight) height = paneHeight;
    return width > 0 && height > 0 && x >= left && x < left + width && y >= top && y < top + height;
}
}
