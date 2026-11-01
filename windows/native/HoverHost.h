#pragma once
#include <inspectable.h>
#include <memory>
#include "Hover.h"
namespace dt {
std::shared_ptr<void> TrackToolbar(IInspectable* object) noexcept;
HRESULT CompactToolbarRow(std::shared_ptr<void> const& region, bool compact) noexcept;
HoverSample ReadToolbarHover(std::shared_ptr<void> const& region, bool revealed) noexcept;
}
