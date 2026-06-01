#pragma once

#include <cstdint>

namespace UI::FavoritesMenu {
enum class RowRefreshMode : std::uint8_t {
    kChangedRowsOnly,
    kForceRedraw,
};

void QueueRingRowRefresh(RowRefreshMode a_mode = RowRefreshMode::kChangedRowsOnly);
}
