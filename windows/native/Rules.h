#pragma once
#include <string_view>

namespace dt {
// Keep existing preference bits stable across upgrades.
enum Target : unsigned {
    None = 0, Chat = 1, Sidebar = 2, Downloads = 4, Bookmark = 8,
    Extensions = 16, ExtensionsOverflow = 32, Toolbar = 64, BookmarksBar = 128
};
enum class Family : unsigned { Button, Repeater, Host };
struct NativeTarget { Target flag; const wchar_t* id; Family family; bool optional; };
inline constexpr NativeTarget NativeTargets[] = {
    {Chat, L"ChatButton", Family::Button, false},
    {Sidebar, L"SidebarToggleButton", Family::Button, false},
    {Downloads, L"DownloadsButton", Family::Button, false},
    {Bookmark, L"BookmarkButton", Family::Button, false},
    {Extensions, L"PinnedExtensionsHost", Family::Host, false},
    {Toolbar, L"NavigationBarHost", Family::Host, false},
    {BookmarksBar, L"BookmarksBarHost", Family::Host, true}
};
// Bit 32 remains accepted for preferences written by protocol 7.
// The host contains both the repeater and its overflow; do not patch those children.
inline constexpr unsigned ExtensionParts = Extensions | ExtensionsOverflow;
inline constexpr unsigned ToolbarParts = Toolbar | BookmarksBar;
inline constexpr unsigned All = Chat | Sidebar | Downloads | Bookmark | ExtensionParts | ToolbarParts;
inline constexpr unsigned UnsupportedFlag = 1u << 30;
struct Tweak {
    unsigned mask, required;
    const wchar_t* key;
    const wchar_t* option;
    const wchar_t* title;
    const wchar_t* detail;
};
inline constexpr Tweak Tweaks[] = {
    {ToolbarParts, Toolbar, L"HideToolbar", L"--hide-toolbar", L"Hide toolbar",
     L"Hide the address and navigation toolbar and bookmarks bar. Experimental."},
    {Chat, Chat, L"HideChat", L"--hide-chat", L"Hide Chat button", L"Remove the Chat button from the toolbar."},
    {Sidebar, Sidebar, L"HideSidebar", L"--hide-sidebar", L"Hide sidebar button", L"Remove the button that opens and closes the sidebar."},
    {Downloads, Downloads, L"HideDownloads", L"--hide-downloads", L"Hide downloads button", L"Remove the downloads button from the toolbar."},
    {Bookmark, Bookmark, L"HideBookmark", L"--hide-bookmark", L"Hide bookmark button", L"Remove the bookmark button from the toolbar."},
    {ExtensionParts, Extensions, L"HideExtensions", L"--hide-extensions", L"Hide extensions",
     L"Hide pinned extension buttons and their overflow button."}
};
inline Target Identify(std::wstring_view id, Family family = Family::Button) {
    for (auto const& item : NativeTargets) if (item.family == family && id == item.id) return item.flag;
    return None;
}
inline Target IdentifyHost(std::wstring_view name) { return Identify(name, Family::Host); }
inline bool IsButtonType(std::wstring_view type) {
    return type == L"Microsoft.UI.Xaml.Controls.Button" || type == L"Button";
}
inline bool IsRepeaterType(std::wstring_view type) {
    return type == L"Microsoft.UI.Xaml.Controls.ItemsRepeater" || type == L"ItemsRepeater";
}
inline bool IsHostType(std::wstring_view type) {
    return type == L"Microsoft.UI.Xaml.Controls.ContentPresenter" || type == L"ContentPresenter";
}
// A nonempty explicit AutomationId takes precedence over x:Name.
inline Target IdentifyButton(std::wstring_view id, Target namedTarget, Family family = Family::Button) {
    return id.empty() ? namedTarget : Identify(id, family);
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
