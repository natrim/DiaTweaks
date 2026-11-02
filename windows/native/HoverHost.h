#pragma once
#include <inspectable.h>
#include <memory>
#include <string>
#include "Hover.h"
namespace dt {
std::shared_ptr<void> TrackToolbar(IInspectable* object) noexcept;
HRESULT CompactToolbarRow(std::shared_ptr<void> const& region, bool compact) noexcept;
HRESULT FlushToolbarLayout(std::shared_ptr<void> const& region) noexcept;
std::wstring ToolbarLayoutTrace(std::shared_ptr<void> const& region);
HoverSample ReadToolbarHover(std::shared_ptr<void> const& region, bool revealed) noexcept;
}
