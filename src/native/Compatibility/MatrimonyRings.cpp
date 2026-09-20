#include "Compatibility/MatrimonyRings.h"

#include <RE/Skyrim.h>
#include <RE/T/TESFile.h>

#include <ClibUtil/string.hpp>

#include <algorithm>
#include <array>

namespace Compatibility {
bool IsBondOfMatrimony(const RE::TESObjectARMO* a_ring) {
    if (!a_ring) {
        return false;
    }
    if (a_ring->GetFormID() == 0x000C5809) {
        return true;
    }

    constexpr std::array<RE::FormID, 3> kUpgradeFormIDs {0x801, 0x802, 0x804};
    const auto* file = a_ring->GetFile(0);
    return file
           != nullptr
           && clib_util::string::iequals(file->GetFilename(), "UpgradableBondOfMatrimony.esp")
           && std::ranges::contains(kUpgradeFormIDs, a_ring->GetLocalFormID());
}
}
