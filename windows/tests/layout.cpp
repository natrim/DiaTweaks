#include "FakeTree.h"
#include "../native/LayoutClient.h"
int main() {
    try {
        Check(dt::DetachProtocolVersion==10, "detach compatibility must remain at version 10");
        Check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM");
        module=GetModuleHandleW(nullptr);
        auto fake=new FakeTree;
        auto tap=new Tap;
        Check(SUCCEEDED(tap->SetSite(static_cast<IVisualTreeService*>(fake))),"attach report test");
        HWND window=FindWindowExW(HWND_MESSAGE,nullptr,dt::WindowClass,nullptr);
        auto deadline=GetTickCount64()+5000;
        while(GetTickCount64()<deadline) {
            Pump();
            if(SendMessageW(window,dt::State,0,0)==1 && (SendMessageW(window,dt::Query,dt::Sidebar,0)&0xffff)) break;
            Sleep(5);
        }
        Check(SendMessageW(window,dt::Configure,dt::Chat,0)==0,"seed active choice");
        unsigned writes=fake->writes;
        auto report=dt::layout::Capture(window);
        Check(report.find(L"fake page frame report")!=std::wstring::npos && report.find(L"fake geometry")!=std::wstring::npos,"page and toolbar geometry transported across reply window");
        Check(fake->writes==writes && SendMessageW(window,dt::Flags,0,0)==dt::Chat,"inspection is read-only");
        bool failed=false;
        try { dt::layout::Capture(nullptr,20); } catch(std::exception const&) { failed=true; }
        Check(failed,"missing session produces an error");
        failed=false;
        try { dt::layout::Capture(window,0); } catch(std::exception const&) { failed=true; }
        Check(failed,"no reply produces a bounded timeout");
        // Drain the deliberately timed-out request before finishing.
        Pump();
        std::cout << "Read-only page/toolbar layout transport, detach compatibility and error checks passed\n";
        return 0;
    } catch(std::exception const& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
