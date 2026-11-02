#pragma once
#include <windows.h>
#include <memory>
#include <string>
namespace dt {
std::shared_ptr<void> CreateNativeViewportGuard();
HRESULT UpdateNativeViewportGuard(std::shared_ptr<void> const&, HWND owner, RECT client, bool discover = true) noexcept;
void SuspendNativeViewportGuard(std::shared_ptr<void> const&) noexcept;
std::wstring NativeViewportGuardReport(std::shared_ptr<void> const&);
}
