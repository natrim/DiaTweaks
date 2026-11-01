#pragma once
#include <string_view>

namespace dt {
enum Target : unsigned { None = 0, Chat = 1, Sidebar = 2 };
inline constexpr unsigned All = Chat | Sidebar;
inline Target Identify(std::wstring_view automationId) {
    if (automationId == L"ChatButton") return Chat;
    if (automationId == L"SidebarToggleButton") return Sidebar;
    return None;
}
inline bool IsProperty(std::wstring_view name, std::wstring_view property) {
    return name == property || (name.size() > property.size() &&
        name[name.size() - property.size() - 1] == L'.' &&
        name.substr(name.size() - property.size()) == property);
}
inline bool IsVisible(std::wstring_view value) { return value == L"Visible" || value == L"0"; }
inline bool IsCollapsed(std::wstring_view value) { return value == L"Collapsed" || value == L"1"; }
// Refuse bindings, value handles, animations and visual-state overrides.
inline bool CanOverride(bool plain, bool simpleSource, bool hasBinding, std::wstring_view value) {
    return plain && simpleSource && !hasBinding && IsVisible(value);
}
}
