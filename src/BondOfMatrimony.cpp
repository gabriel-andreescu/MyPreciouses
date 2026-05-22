#include "BondOfMatrimony.h"

#include "Forms.h"

namespace BondOfMatrimony {
namespace {
    RE::TESObjectARMO* g_bondOfMatrimony {nullptr};
}

void Load() {
    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler) {
        logger::warn("Bond: lookup failed | reason=noDataHandler");
        g_bondOfMatrimony = nullptr;
        return;
    }

    g_bondOfMatrimony = dataHandler->LookupForm<RE::TESObjectARMO>(Forms::kBondOfMatrimonyFormID, Forms::kSkyrimESM);
    if (!g_bondOfMatrimony) {
        logger::warn("Bond: lookup failed | plugin={} | form={:06X}", Forms::kSkyrimESM, Forms::kBondOfMatrimonyFormID);
        return;
    }

    logger::info("Bond: resolved | form={:08X}", g_bondOfMatrimony->GetFormID());
}

RE::TESObjectARMO* Get() {
    return g_bondOfMatrimony;
}

bool IsBond(const RE::TESObjectARMO* a_armor) {
    return a_armor && a_armor == g_bondOfMatrimony;
}

bool IsBond(const RE::FormID a_formID) {
    return g_bondOfMatrimony && g_bondOfMatrimony->GetFormID() == a_formID;
}
}
