#pragma once

#include <optional>

namespace MeshRetargeting {
struct AttachContext {
    RE::ActorHandle actorHandle;
    RE::BSTSmartPointer<RE::BipedAnim> biped;
    RE::TESObjectARMO* armor {nullptr};
    RE::TESObjectARMA* addon {nullptr};
    std::int32_t slot {-1};
};

[[nodiscard]] std::optional<AttachContext> CaptureAttachContext(
    RE::NiAVObject* a_clonedNode,
    RE::NiAVObject* a_node,
    std::int32_t a_slot,
    RE::TESObjectREFR* a_actor,
    RE::BSTSmartPointer<RE::BipedAnim>& a_biped
);
void QueueReplacement(AttachContext a_context);
}
