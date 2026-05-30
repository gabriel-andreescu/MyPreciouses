#pragma once

#include "Core/ActorKey.h"
#include "Core/FingerMask.h"
#include "Core/Target.h"

#include <vector>

namespace Visuals::Attachments {
struct AttachmentSource {
    Core::Target target {Core::kDefaultLeftTarget};
    Core::FingerMask sourceFingerMask;
    RE::FormID sourceFormID {0};
};

void EnableFirstPersonRingSlotForRaces();
void RequestRefresh(Core::ActorKey a_actor, std::vector<AttachmentSource> a_sources);
void Revert();
}
