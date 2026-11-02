#include <iostream>
#include <stdexcept>
#include "../native/Rules.h"
#include "../native/Scope.h"
#include "../native/Hover.h"
#include "../native/PagePolicy.h"

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
    struct Node { const wchar_t* name; Node* parent; bool peers = false; };
    Node root{L"SidebarPresenter", nullptr}, header{L"WindowDragHeader", &root}, button{L"OverflowButton", &header};
    auto inHeader = [](Node* node) {
        return dt::InSidebarHeader(node, [](Node* item) { return item->parent; }, [](Node* item) { return item->name; }, [](Node* item) { return item->peers; });
    };
    Check(inHeader(&button), "sidebar header ancestry");
    button.parent = &root;
    Check(!inHeader(&button), "sidebar alone is not enough");
    header.parent = nullptr; button.parent = &header;
    Check(!inHeader(&button), "unrelated drag header is not enough");
    header.parent = &button;
    Check(!inHeader(&button), "cyclic ancestor walk is bounded");
    root.name = L"SpacesScrollViewer"; header.name = L""; header.parent = &root; header.peers = true;
    Check(inHeader(&button), "current unnamed header under SpacesScrollViewer is recognized by peer buttons");
    header.peers = false;
    Check(!inHeader(&button), "sidebar container without header peers is not enough");
    header.peers = true; header.name = L"RowsPanel";
    Check(!inHeader(&button), "tab content cannot borrow header peers from above");
    header.name = L""; root.name = L"UnrelatedContainer";
    Check(!inHeader(&button), "header peers outside sidebar remain excluded");
    Check(dt::HiddenWithToolbar(dt::Chat, dt::Toolbar) &&
          !dt::HiddenWithToolbar(dt::RecentTabs, dt::Toolbar) &&
          !dt::HiddenWithToolbar(dt::Spaces, dt::Toolbar), "toolbar dependency excludes sidebar features");
    dt::HoverState hover;
    Check(!hover.Update({true, true, false, true}, 1), "stale focus cannot reveal an initially hidden toolbar");
    Check(hover.Update({true, true, true, false}, 10), "pointer reveals toolbar");
    Check(hover.Update({true, true, false, true}, 20), "toolbar focus holds reveal");
    Check(hover.Update({true, true, false, false}, 30) && hover.Update({true, true, false, false}, 279), "hide delay prevents edge flicker");
    Check(!hover.Update({true, true, false, false}, 280), "hide after delay");
    Check(hover.Update({true, true, true, false}, 300) && !hover.Update({true, false, true, true}, 301), "inactive window resets reveal");
    Check(dt::InHoverBand(12, 43, 10, 20, 100, 500, 56, false) &&
          !dt::InHoverBand(12, 44, 10, 20, 100, 500, 56, false), "24-DIP minimum trigger");
    Check(dt::InHoverBand(12, 91, 10, 20, 100, 500, 56, true) &&
          !dt::InHoverBand(12, 92, 10, 20, 100, 500, 56, true), "revealed toolbar plus bottom margin");
    Check(!dt::InHoverBand(9, 22, 10, 20, 100, 500, 56, false), "other pane excluded");
    Check(dt::InHoverBand(12, 90, 10, 20, 100, 500, 56, false, 55) &&
          !dt::InHoverBand(12, 91, 10, 20, 100, 500, 56, false, 55), "blank strip plus 16-DIP page edge is covered");
    Check(!dt::InHoverBand(12, 148, 10, 20, 100, 500, 56, false, 1000), "unrelated large page offset cannot create a huge trigger");
    Check(!dt::InHoverBand(12, 30, 10, 20, 100, 10, 56, false, 55), "trigger stays within pane height");
    auto stripped = dt::page::Strip({6, 55, -6, 8});
    Check(stripped.left == 0 && stripped.top == 55 && stripped.right == -6 && stripped.bottom == 0, "remove only small positive page padding");
    Check(dt::page::Gap(6.01) == 6 && dt::page::Gap(55) == 0 && dt::page::Gap(-6) == 0, "stable padding gaps preserve toolbar and non-padding offsets");
    // Sanitized topology of the reported WinUI layout: rounded Grid at depth 1,
    // rounded Border at 4, outer inset Grid at 11, wider sidebar shell outside.
    dt::page::Box pageBox{200, 4, 1200, 792};
    dt::page::FrameCandidate ancestors[12]{};
    for (auto& candidate : ancestors) candidate.bounds = pageBox;
    ancestors[10].margin = {0, 4, 4, 4};
    ancestors[11].bounds = {0, 0, 1404, 800};
    Check(dt::page::FrameCount(pageBox, ancestors) == 11, "include rounded Grid and Border through depth 11 inset wrapper");
    ancestors[10].margin = {};
    Check(dt::page::FrameCount(pageBox, ancestors) == 11, "wider sidebar shell stops the walk without an inset");
    ancestors[1].margin = {0, 4, 4, 4};
    Check(dt::page::FrameCount(pageBox, ancestors) == 2, "outer inset is included but ancestors beyond it are excluded");
    Check(dt::page::FrameAncestor({200, 44, 1200, 752}, {200, 4, 1200, 792}), "revealed toolbar remains inside frame scope");
    Check(!dt::page::FrameAncestor(pageBox, {0, 0, 1404, 800}), "sidebar and window shell are excluded");
    auto topOnly = dt::page::FrameMargin({0, 4, 4, 4}, false, true);
    Check(topOnly.left == 0 && topOnly.top == 0 && topOnly.right == 4 && topOnly.bottom == 4, "hidden toolbar removes only the real top inset");
    auto edgeMargin = dt::page::FrameMargin({0, 4, 4, 4}, true, false);
    Check(edgeMargin.left == 0 && edgeMargin.top == 0 && edgeMargin.right == 0 && edgeMargin.bottom == 0, "edge layout removes actual outer margin without shifting left");
    auto nativeMargin = dt::page::FrameMargin({0, 4, 4, 4}, false, false);
    Check(nativeMargin.top == 4 && nativeMargin.right == 4 && nativeMargin.bottom == 4, "disabled modes preserve original insets");
    Check(dt::page::HasInset({0, 4, 4, 4}) && !dt::page::HasInset({0, 40, -4, 0}), "walk stops at a small native inset only");
    auto corners = dt::page::Square({8, 9, 10, 11}, true, false);
    Check(corners.tl == 0 && corners.bl == 0 && corners.tr == 9 && corners.br == 10, "square only sidebar-facing corners");
    corners = dt::page::Square({8, 9, 10, 11}, false, true);
    Check(corners.tl == 8 && corners.bl == 11 && corners.tr == 0 && corners.br == 0, "right sidebar preserves opposite corners");
    Check(dt::MissingDependency(dt::SquareSidebar, 0) == dt::EdgeToEdge && dt::MissingDependency(dt::SquareSidebar, dt::EdgeToEdge) == 0,
          "square sidebar corners depend on edge-to-edge");
    Check(!dt::HiddenWithToolbar(dt::EdgeToEdge, dt::Toolbar) && !dt::HiddenWithToolbar(dt::SquareSidebar, dt::Toolbar), "page options are independent of toolbar hiding");
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
