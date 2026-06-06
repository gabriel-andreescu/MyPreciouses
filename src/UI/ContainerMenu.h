#pragma once

#include "Core/ActorKey.h"

#include <optional>

namespace RE {
class ContainerMenu;
class GFxMovieView;
}

namespace UI::ContainerMenu {
[[nodiscard]] bool IsOpenMovie(const RE::GFxMovieView* a_view);
[[nodiscard]] std::optional<Core::ActorKey> ResolveVisibleActorKey(RE::GFxMovieView* a_view);
void OnClosed();
void OnShown(RE::ContainerMenu& a_containerMenu);
void OnInventoryUpdateProcessed(RE::ContainerMenu& a_containerMenu);
[[nodiscard]] bool TryRefreshOpenMenuRows();
}
