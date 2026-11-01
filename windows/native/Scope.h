#pragma once
#include <string_view>

namespace dt {
// Match the sidebar's header only. OverflowButton is also used by other menus.
// Bound the walk so malformed/cyclic trees cannot hang the UI thread.
template<class Node, class Parent, class Name>
bool InSidebarHeader(Node node, Parent parent, Name name) {
    bool header = false, sidebar = false;
    for (unsigned depth = 0; node && depth < 64; ++depth, node = parent(node)) {
        auto label = name(node);
        std::wstring_view text(label);
        header = header || text == L"WindowDragHeader";
        sidebar = sidebar || text == L"SidebarPresenter";
        if (header && sidebar) return true;
    }
    return false;
}
}
