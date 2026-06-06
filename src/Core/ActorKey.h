#pragma once

#include <cstdint>
#include <functional>

namespace Core {
inline constexpr RE::FormID kPlayerFormID {0x14};

struct ActorKey {
    RE::FormID referenceFormID {0};

    [[nodiscard]] explicit operator bool() const {
        return referenceFormID != 0;
    }

    [[nodiscard]] bool operator==(const ActorKey&) const = default;
};

[[nodiscard]] inline ActorKey MakeActorKey(const RE::Actor& a_actor) {
    return ActorKey {
        .referenceFormID = a_actor.GetFormID(),
    };
}

[[nodiscard]] inline ActorKey GetPlayerActorKey() {
    const auto* player = RE::PlayerCharacter::GetSingleton();
    return player ? MakeActorKey(*player) : ActorKey {};
}

[[nodiscard]] inline bool IsPlayerActorKey(const ActorKey a_actor) {
    const auto player = GetPlayerActorKey();
    return (player && a_actor == player) || a_actor.referenceFormID == kPlayerFormID;
}

[[nodiscard]] inline RE::Actor* ResolveActor(const ActorKey a_actor) {
    return a_actor ? RE::TESForm::LookupByID<RE::Actor>(a_actor.referenceFormID) : nullptr;
}
}

template <>
struct std::hash<Core::ActorKey> {
    [[nodiscard]] std::size_t operator()(const Core::ActorKey a_actor) const noexcept {
        return std::hash<RE::FormID> {}(a_actor.referenceFormID);
    }
};
