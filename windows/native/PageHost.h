#pragma once
#include <windows.h>
#include <inspectable.h>
#include <memory>
#include <string>
namespace dt {
struct PageApi { void* context; bool (*plain)(void*, IInspectable*, wchar_t const*); };
std::shared_ptr<void> CreatePages(PageApi api);
void ObservePage(std::shared_ptr<void> const&, unsigned long long node, IInspectable*) noexcept;
void ForgetPage(std::shared_ptr<void> const&, unsigned long long node) noexcept;
HRESULT ConfigurePages(std::shared_ptr<void> const&, unsigned flags) noexcept;
void TickPages(std::shared_ptr<void> const&) noexcept;
unsigned QueryPages(std::shared_ptr<void> const&, unsigned feature) noexcept;
unsigned OwnedPages(std::shared_ptr<void> const&, unsigned feature = 0) noexcept;
HRESULT PagesError(std::shared_ptr<void> const&) noexcept;
std::wstring PagesLayoutReport(std::shared_ptr<void> const&);
std::wstring PagesErrorText(std::shared_ptr<void> const&);
}
