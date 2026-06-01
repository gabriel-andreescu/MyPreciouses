#pragma once

#include <cstdint>

namespace RE {
class BipedAnim;
class NiAVObject;
}

namespace RingVisuals {
void RequestRefresh();
void Refresh();
void Revert();
void RetargetVanillaRingClone(RE::BipedAnim* a_biped, RE::NiAVObject* a_clone, std::int32_t a_slot);
}
