#pragma once
#include <inspectable.h>
#include <memory>
#include <string>
#include "Hover.h"
namespace dt {
struct ToolbarPolicyApi { void* context; void (*changed)(void*); };
void WatchToolbarPolicy(std::shared_ptr<void> const&, ToolbarPolicyApi) noexcept;
std::shared_ptr<void> TrackToolbar(IInspectable* object) noexcept;
HRESULT SetToolbarOverlay(std::shared_ptr<void> const& region, bool enabled) noexcept;
HRESULT CompactToolbarRow(std::shared_ptr<void> const& region, bool compact) noexcept;
bool InheritToolbarViewport(std::shared_ptr<void> const&, std::shared_ptr<void> const&) noexcept;
void ReattachToolbarViewport(std::shared_ptr<void> const&) noexcept;
void RetireToolbarViewport(std::shared_ptr<void> const&) noexcept;
void DetachToolbarViewport(std::shared_ptr<void> const& region) noexcept;
HRESULT FlushToolbarLayout(std::shared_ptr<void> const& region) noexcept;
std::wstring ToolbarLayoutTrace(std::shared_ptr<void> const& region);
HoverSample ReadToolbarHover(std::shared_ptr<void> const& region, bool revealed) noexcept;
}
