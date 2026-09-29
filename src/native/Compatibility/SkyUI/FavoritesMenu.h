#pragma once

#include <RE/Skyrim.h> // IWYU pragma: keep

#include "Equipment/FavoriteGroups.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace Compatibility::SkyUI::FavoritesMenu {
void Attach(RE::FavoritesMenu& a_menu, RE::GFxValue& a_itemList);
[[nodiscard]] std::optional<std::vector<Equipment::FavoriteGroups::Row>> TakeCapture(std::size_t a_group);
void Revert();
}
