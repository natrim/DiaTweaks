#pragma once

namespace dt::page {
inline bool SidebarCorner(unsigned corner, bool left, bool right) { return corner == 0 || corner == 3 ? left : right; }
struct Radius {
    float x = 0, y = 0;
    bool operator==(Radius const&) const = default;
};
// Ownership is per corner. Restoring one corner must not overwrite a radius
// changed by Dia, or the other side of the same RectangleClip.
struct RadiusOverride {
    Radius original{}, applied{};
    bool owned = false;
    template<class Read, class Write> bool Update(bool square, Read read, Write write) {
        auto current = read();
        if (owned && current != applied) owned = false;
        if (!square) {
            if (!owned) return true;
            write(original);
            if (read() != original) return false;
            owned = false;
            return true;
        }
        if (current == Radius{}) return true;
        original = current;
        applied = {};
        // Record ownership before writing so a setter that changes the value
        // and then throws still leaves a restorable snapshot.
        owned = true;
        write(applied);
        return read() == applied;
    }
};
}
