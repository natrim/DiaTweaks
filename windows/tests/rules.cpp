#include <iostream>
#include <stdexcept>
#include "../native/Rules.h"
#include "../native/Scope.h"

void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    // Exact matches from Dia 1.5.0.588's UIA inspection. Do not hide label children,
    // captions, navigation controls, extensions or similarly named future controls.
    Check(dt::Identify(L"ChatButton") == dt::Chat, "chat selector");
    Check(dt::Identify(L"SidebarToggleButton") == dt::Sidebar, "sidebar selector");
    Check(dt::IdentifyButton(L"", dt::Chat) == dt::Chat, "UIA ID can come from x:Name");
    Check(dt::IdentifyButton(L"OtherButton", dt::Chat) == dt::None, "explicit unknown ID overrides known x:Name");
    Check(dt::IdentifyButton(L"SidebarToggleButton", dt::Chat) == dt::Sidebar, "explicit ID takes precedence");
    Check(dt::IsButtonType(L"Button") && dt::IsButtonType(L"Microsoft.UI.Xaml.Controls.Button"), "short and full type names");
    Check(!dt::IsButtonType(L"Microsoft.UI.Xaml.Controls.TextBlock"), "a named label is not a button");
    for (auto id : {L"ChatButtonLabel", L"chatbutton", L"OtherChatButton", L"SidebarToggleButton2",
                    L"BackButton", L"CloseButton", L"", L"ChatButton\n"})
        Check(dt::Identify(id) == dt::None, "unrelated UI must remain untouched");
    Check(dt::Identify(L"DownloadsButton") == dt::Downloads, "downloads selector");
    Check(dt::Identify(L"BookmarkButton") == dt::Bookmark, "bookmark selector");
    Check(dt::Identify(L"PinnedExtensionsOverflowButton") == dt::None, "overflow left to its parent host");
    Check(dt::IdentifyHost(L"PinnedExtensionsHost") == dt::Extensions, "extensions wrapper selector");
    Check(dt::Identify(L"PinnedExtensionsRepeater", dt::Family::Repeater) == dt::None, "bound repeater untouched");
    Check(dt::Identify(L"NavigationBarHost", dt::Family::Host) == dt::Toolbar, "navigation host");
    Check(dt::Identify(L"BookmarksBarHost", dt::Family::Host) == dt::BookmarksBar, "bookmarks host");
    Check(dt::Identify(L"PinnedExtensionsRepeater") == dt::None &&
          dt::Identify(L"NavigationBarHost") == dt::None &&
          dt::Identify(L"ChatButton", dt::Family::Host) == dt::None, "selector type boundaries");
    Check(dt::Identify(L"OverflowButton") == dt::None &&
          dt::Identify(L"NavigationChromeHost", dt::Family::Host) == dt::None, "unrelated menu and info-bar parent excluded");
    Check(dt::Identify(L"SpaceSwitcherButton") == dt::Spaces, "Spaces selector");
    Check(dt::Identify(L"OverflowButton", dt::Family::Button, true) == dt::RecentTabs, "scoped recent-tabs selector");
    Check(dt::IdentifyButton(L"", dt::RecentTabs) == dt::None, "unscoped x:Name cannot match recent tabs");
    struct Node { const wchar_t* name; Node* parent; };
    Node root{L"SidebarPresenter", nullptr}, header{L"WindowDragHeader", &root}, button{L"OverflowButton", &header};
    auto inHeader = [](Node* node) {
        return dt::InSidebarHeader(node, [](Node* item) { return item->parent; }, [](Node* item) { return item->name; });
    };
    Check(inHeader(&button), "sidebar header ancestry");
    button.parent = &root;
    Check(!inHeader(&button), "sidebar alone is not enough");
    header.parent = nullptr; button.parent = &header;
    Check(!inHeader(&button), "unrelated drag header is not enough");
    header.parent = &button;
    Check(!inHeader(&button), "cyclic ancestor walk is bounded");
    Check(dt::HiddenWithToolbar(dt::Chat, dt::Toolbar) &&
          !dt::HiddenWithToolbar(dt::RecentTabs, dt::Toolbar) &&
          !dt::HiddenWithToolbar(dt::Spaces, dt::Toolbar), "toolbar dependency excludes sidebar features");
    unsigned covered = 0;
    for (auto const& tweak : dt::Tweaks) {
        Check((covered & tweak.mask) == 0 && (tweak.required & tweak.mask) == tweak.required, "disjoint catalog");
        covered |= tweak.mask;
    }
    Check(covered == dt::All && (dt::All & dt::UnsupportedFlag) == 0, "catalog covers persisted mask");
    Check(dt::IsProperty(L"Microsoft.UI.Xaml.Automation.AutomationProperties.AutomationId", L"AutomationId"), "attached property");
    Check(!dt::IsProperty(L"NotAutomationId", L"AutomationId"), "property suffix boundary");
    Check(dt::CanOverride(true, true, false, L"Visible"), "plain visible property");
    Check(dt::CanOverride(true, true, false, L"0"), "numeric visible property");
    Check(!dt::CanOverride(false, true, false, L"Visible"), "do not replace object handles");
    Check(!dt::CanOverride(true, true, true, L"Visible"), "do not destroy bindings");
    Check(!dt::CanOverride(true, false, false, L"Visible"), "do not replace animations or states");
    Check(!dt::CanOverride(true, true, false, L"unexpected"), "reject unknown enum values");
    Check(dt::IsCollapsed(L"Collapsed") && dt::IsCollapsed(L"1") && !dt::IsCollapsed(L"Visible"), "readback enum");
    std::cout << "Windows selector and property policy checks passed\n";
}
