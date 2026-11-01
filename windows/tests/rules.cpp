#include <iostream>
#include <stdexcept>
#include "../native/Rules.h"

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
                    L"BackButton", L"BookmarkButton", L"DownloadsButton", L"CloseButton", L"", L"ChatButton\n"})
        Check(dt::Identify(id) == dt::None, "unrelated UI must remain untouched");
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
