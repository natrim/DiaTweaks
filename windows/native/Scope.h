#pragma once
#include <string_view>

namespace dt {
inline bool IsScopeMarker(std::wstring_view name) {
    return name == L"WindowDragHeader" || name == L"SidebarPresenter" || name == L"SpacesScrollViewer" ||
        name == L"TabListScrollViewer" || name == L"RowsPanel" || name == L"TabStripContentHost" ||
        name == L"AppMenuButton" || name == L"SpaceSwitcherButton";
}
// OverflowButton also names unrelated menus. Require a sidebar container and
// a header below it; never accept a tab-row/content branch as a header.
template<class Node, class Parent, class Name, class Peers>
bool InSidebarHeader(Node node, Parent parent, Name name, Peers headerPeers) {
    bool header = false;
    for (unsigned depth = 0; node && depth < 64; ++depth, node = parent(node)) {
        auto label = name(node);
        std::wstring_view text(label);
        if (text == L"TabListScrollViewer" || text == L"RowsPanel" || text == L"TabStripContentHost") return false;
        bool boundary = text == L"SidebarPresenter" || text == L"SpacesScrollViewer";
        if (boundary) return header;
        header = header || text == L"WindowDragHeader" || headerPeers(node);
    }
    return false;
}
template<class Node, class Parent, class Name>
bool InSidebarHeader(Node node, Parent parent, Name name) {
    return InSidebarHeader(node, parent, name, [](Node) { return false; });
}
}
