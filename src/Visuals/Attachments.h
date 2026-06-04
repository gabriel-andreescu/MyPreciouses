#pragma once

#include "Core/ActorKey.h"
#include "Core/FingerMask.h"
#include "Core/Target.h"

#include <cstdint>
#include <vector>

namespace RE {
class BipedAnim;
class NiAVObject;
}

namespace Visuals::Attachments {
struct AttachmentSource {
    Core::Target target {Core::kDefaultLeftTarget};
    Core::FingerMask sourceFingerMask;
    RE::FormID sourceFormID {0};
};

void EnableFirstPersonRingSlotForRaces();
[[nodiscard]] bool RetargetVanillaRingClone(
    RE::BipedAnim* a_biped,
    RE::NiAVObject* a_object,
    std::int32_t a_bipedObjectSlot
);
void RequestRefresh(Core::ActorKey a_actor, std::vector<AttachmentSource> a_sources);
void Revert();
}
