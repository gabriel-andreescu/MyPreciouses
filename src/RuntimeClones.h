#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "Serialization.h"
#include "Slots.h"

namespace RuntimeClones {
struct CloneKey {
    DisplaySlot channel {DisplaySlot::kRegular};
    RE::FormID sourceArmorFormID {0};

    [[nodiscard]] bool operator==(const CloneKey&) const = default;
};

struct CloneRecord {
    DisplaySlot channel {DisplaySlot::kRegular};
    RE::FormID sourceArmorFormID {0};
    RE::FormID cloneArmorFormID {0};
    std::vector<RE::FormID> sourceAddonFormIDs;
};

[[nodiscard]] RE::TESObjectARMO* DuplicateArmor(RE::TESObjectARMO& a_source);
[[nodiscard]] RE::TESObjectARMA* DuplicateAddon(RE::TESObjectARMA& a_source);
[[nodiscard]] bool RegisterForm(RE::TESForm& a_form, std::string_view a_kind, RE::FormID a_sourceFormID);
void ConfigureArmor(
    const RE::TESObjectARMO& a_source,
    RE::TESObjectARMO& a_armor,
    DisplaySlot a_channel = DisplaySlot::kRegular
);
void ConfigureAddon(
    const RE::TESObjectARMA& a_source,
    RE::TESObjectARMA& a_addon,
    DisplaySlot a_channel = DisplaySlot::kRegular
);
void CopyRaceCoverage(const RE::TESObjectARMA& a_sourceAddon, RE::TESObjectARMA& a_clonedAddon);
void Register(const CloneRecord& a_record);
void RequireRestore(DisplaySlot a_channel, RE::FormID a_sourceArmorFormID);
void ClearRestore(DisplaySlot a_channel, RE::FormID a_sourceArmorFormID = 0);
[[nodiscard]] bool RequiresRestore(DisplaySlot a_channel, RE::FormID a_sourceArmorFormID);
[[nodiscard]] std::optional<CloneRecord> GetRecord(DisplaySlot a_channel, RE::FormID a_sourceArmorFormID);
[[nodiscard]] std::optional<RE::FormID> FindSourceArmorFormID(const RE::TESObjectARMO& a_clone);
void MarkRestored(DisplaySlot a_channel, RE::FormID a_sourceArmorFormID);
void Discard(DisplaySlot a_channel, RE::FormID a_sourceArmorFormID);
void Save(SKSE::SerializationInterface& a_intfc, const std::vector<CloneKey>& a_selectedSources);
bool LoadRecord(Serialization::RecordInfo a_recordInfo, SKSE::SerializationInterface& a_intfc);
void Revert(DisplaySlot a_channel);
void Revert();
}
