#pragma once
#include <cstdint>
#include <deque>
#include <string>
#include <sstream>
#include <utility>

namespace dt {
// Stores geometry supplied by the caller, never UI text or page contents.
struct ViewportTrace {
    struct Entry { std::uint64_t time; std::wstring source, geometry; };
    static constexpr size_t Capacity = 96;
    std::deque<Entry> entries;
    void Add(std::uint64_t time, std::wstring source, std::wstring geometry, bool force = false) {
        if (!force && !entries.empty() && entries.back().geometry == geometry) return;
        if (entries.size() == Capacity) entries.pop_front();
        entries.push_back({time, std::move(source), std::move(geometry)});
    }
    std::wstring Report() const {
        std::wostringstream out;
        out << L"VIEWPORT TIMING (geometry only, last " << entries.size() << L" changes)\n";
        if (!entries.empty()) for (auto const& entry : entries)
            out << L'+' << entry.time - entries.front().time << L"ms " << entry.source << L'\n' << entry.geometry;
        return out.str();
    }
};
}
