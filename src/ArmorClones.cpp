#include "ArmorClones.h"

#include "Slots.h"

#include <mutex>
#include <ranges>
#include <unordered_map>
#include <unordered_set>

namespace ArmorClones {
namespace {
    constexpr auto kRecordClone = Serialization::MakeRecordType('R', 'C', 'L', 'N');
    constexpr std::uint32_t kCloneVersion = 1;

    struct StoredCloneHeader {
        std::uint32_t channel {std::to_underlying(DisplaySlot::kRegular)};
        RE::FormID sourceArmorFormID {0};
        RE::FormID cloneArmorFormID {0};
        std::uint32_t sourceAddonCount {0};
    };

    struct CloneKeyHash {
        [[nodiscard]] std::size_t operator()(const CloneKey& a_key) const noexcept {
            return (static_cast<std::size_t>(std::to_underlying(a_key.channel)) << 32) | a_key.sourceArmorFormID;
        }
    };

    [[nodiscard]] CloneKey MakeKey(const DisplaySlot a_channel, const RE::FormID a_sourceArmorFormID) {
        return CloneKey {
            .channel = a_channel,
            .sourceArmorFormID = a_sourceArmorFormID,
        };
    }

    [[nodiscard]] std::mutex& RegistryLock() {
        static std::mutex lock;
        return lock;
    }

    [[nodiscard]] std::unordered_map<CloneKey, CloneRecord, CloneKeyHash>& RecordsBySource() {
        static auto* records = new std::unordered_map<CloneKey, CloneRecord, CloneKeyHash>();
        return *records;
    }

    [[nodiscard]] std::unordered_set<CloneKey, CloneKeyHash>& RestoreRequiredSources() {
        static auto* sources = new std::unordered_set<CloneKey, CloneKeyHash>();
        return *sources;
    }

    [[nodiscard]] RE::BGSBipedObjectForm::BipedObjectSlot GetArmorSlotMask(
        const RE::TESObjectARMO& a_source,
        const DisplaySlot a_channel
    ) {
        auto slotMask = a_source.GetSlotMask();
        const auto hadRingSlot = slotMask.all(RE::BGSBipedObjectForm::BipedObjectSlot::kRing);
        slotMask.reset(RE::BGSBipedObjectForm::BipedObjectSlot::kRing);
        if (hadRingSlot) {
            slotMask.set(Slots::GetArmorSlot(a_channel));
        }
        return *slotMask;
    }

    [[nodiscard]] RE::BGSBipedObjectForm::BipedObjectSlot GetAddonSlotMask(
        const RE::TESObjectARMA& a_source,
        const DisplaySlot a_channel
    ) {
        auto slotMask = a_source.GetSlotMask();
        if (slotMask == RE::BGSBipedObjectForm::BipedObjectSlot::kRing) {
            slotMask.reset(RE::BGSBipedObjectForm::BipedObjectSlot::kRing);
            slotMask.set(Slots::GetArmorSlot(a_channel));
        }
        return *slotMask;
    }

    template <class T>
    [[nodiscard]] T* DuplicateForm(T& a_source, std::string_view a_kind) {
        auto* duplicateForm = a_source.CreateDuplicateForm(true, nullptr);
        auto* duplicate = duplicateForm ? duplicateForm->template As<T>() : nullptr;
        if (!duplicate) {
            logger::warn("ArmorClones: duplicate failed | kind={} | source={:08X}", a_kind, a_source.GetFormID());
            return nullptr;
        }

        return duplicate;
    }

    [[nodiscard]] bool ResolveStoredFormID(
        SKSE::SerializationInterface& a_intfc,
        const RE::FormID a_storedFormID,
        RE::FormID& a_resolvedFormID
    ) {
        a_resolvedFormID = 0;
        if (a_storedFormID == 0) {
            return false;
        }

        if (!a_intfc.ResolveFormID(a_storedFormID, a_resolvedFormID)) {
            return false;
        }

        return true;
    }

}

RE::TESObjectARMO* DuplicateArmor(RE::TESObjectARMO& a_source) {
    return DuplicateForm(a_source, "ARMO"sv);
}

RE::TESObjectARMA* DuplicateAddon(RE::TESObjectARMA& a_source) {
    return DuplicateForm(a_source, "ARMA"sv);
}

bool RegisterForm(RE::TESForm& a_form, const std::string_view a_kind, const RE::FormID a_sourceFormID) {
    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler) {
        logger::warn(
            "ArmorClones: register failed | kind={} | source={:08X} | reason=noDataHandler",
            a_kind,
            a_sourceFormID
        );
        return false;
    }

    const auto beforeFormID = a_form.GetFormID();
    if (!dataHandler->AddFormToDataHandler(std::addressof(a_form))) {
        logger::warn(
            "ArmorClones: register failed | kind={} | source={:08X} | clone={:08X} | reason=dataHandlerRejected",
            a_kind,
            a_sourceFormID,
            beforeFormID
        );
        return false;
    }

    return true;
}

void ConfigureArmor(const RE::TESObjectARMO& a_source, RE::TESObjectARMO& a_armor, const DisplaySlot a_channel) {
    a_armor.SetSlotMask(GetArmorSlotMask(a_source, a_channel));
    a_armor.formFlags |= RE::TESObjectARMO::RecordFlags::kNonPlayable;
    a_armor.value = 0;
    a_armor.weight = 0.0F;
}

void ConfigureAddon(const RE::TESObjectARMA& a_source, RE::TESObjectARMA& a_addon, const DisplaySlot a_channel) {
    a_addon.SetSlotMask(GetAddonSlotMask(a_source, a_channel));
}

void CopyRaceCoverage(const RE::TESObjectARMA& a_sourceAddon, RE::TESObjectARMA& a_clonedAddon) {
    a_clonedAddon.race = a_sourceAddon.race;
    a_clonedAddon.additionalRaces.clear();

    for (auto* race : a_sourceAddon.additionalRaces) {
        if (race) {
            a_clonedAddon.additionalRaces.push_back(race);
        }
    }
}

void Register(const CloneRecord& a_record) {
    if (a_record.sourceArmorFormID == 0 || a_record.cloneArmorFormID == 0 || a_record.sourceAddonFormIDs.empty()) {
        logger::warn(
            "ArmorClones: registry record skipped | channel={} | source={:08X} | clone={:08X} | sourceAddons={} | reason=invalidRecord",
            DisplaySlotLabel(a_record.channel),
            a_record.sourceArmorFormID,
            a_record.cloneArmorFormID,
            a_record.sourceAddonFormIDs.size()
        );
        return;
    }

    std::scoped_lock lock(RegistryLock());
    RecordsBySource()[MakeKey(a_record.channel, a_record.sourceArmorFormID)] = a_record;
}

void RequireRestore(const DisplaySlot a_channel, const RE::FormID a_sourceArmorFormID) {
    std::scoped_lock lock(RegistryLock());
    auto& restoreRequiredSources = RestoreRequiredSources();
    if (a_sourceArmorFormID != 0) {
        restoreRequiredSources.insert(MakeKey(a_channel, a_sourceArmorFormID));
    }
}

void ClearRestore(const DisplaySlot a_channel, const RE::FormID a_sourceArmorFormID) {
    std::scoped_lock lock(RegistryLock());
    auto& restoreRequiredSources = RestoreRequiredSources();
    if (a_sourceArmorFormID == 0) {
        std::erase_if(restoreRequiredSources, [a_channel](const CloneKey& a_key) {
            return a_key.channel == a_channel;
        });
        return;
    }

    restoreRequiredSources.erase(MakeKey(a_channel, a_sourceArmorFormID));
}

bool RequiresRestore(const DisplaySlot a_channel, const RE::FormID a_sourceArmorFormID) {
    std::scoped_lock lock(RegistryLock());
    return RestoreRequiredSources().contains(MakeKey(a_channel, a_sourceArmorFormID));
}

std::optional<CloneRecord> GetRecord(const DisplaySlot a_channel, const RE::FormID a_sourceArmorFormID) {
    std::scoped_lock lock(RegistryLock());
    const auto& recordsBySource = RecordsBySource();
    const auto it = recordsBySource.find(MakeKey(a_channel, a_sourceArmorFormID));
    if (it == recordsBySource.end()) {
        return std::nullopt;
    }

    return it->second;
}

std::optional<RE::FormID> FindSourceArmorFormID(const RE::TESObjectARMO& a_clone) {
    std::scoped_lock lock(RegistryLock());
    const auto cloneFormID = a_clone.GetFormID();
    for (const auto& record : RecordsBySource() | std::views::values) {
        if (record.cloneArmorFormID == cloneFormID) {
            return record.sourceArmorFormID;
        }
    }

    return std::nullopt;
}

void MarkRestored(const DisplaySlot a_channel, const RE::FormID a_sourceArmorFormID) {
    std::scoped_lock lock(RegistryLock());
    RestoreRequiredSources().erase(MakeKey(a_channel, a_sourceArmorFormID));
}

void Discard(const DisplaySlot a_channel, const RE::FormID a_sourceArmorFormID) {
    std::scoped_lock lock(RegistryLock());
    const auto key = MakeKey(a_channel, a_sourceArmorFormID);
    RecordsBySource().erase(key);
    RestoreRequiredSources().erase(key);
}

void Save(SKSE::SerializationInterface& a_intfc, const std::vector<CloneKey>& a_selectedSources) {
    if (a_selectedSources.empty()) {
        return;
    }

    std::vector<CloneRecord> records;
    {
        std::scoped_lock lock(RegistryLock());
        const auto& recordsBySource = RecordsBySource();
        for (const auto& selectedSource : a_selectedSources) {
            if (selectedSource.sourceArmorFormID == 0) {
                continue;
            }

            const auto it = recordsBySource.find(selectedSource);
            if (it != recordsBySource.end()) {
                records.push_back(it->second);
                continue;
            }
        }
    }

    if (records.empty()) {
        return;
    }

    for (const auto& record : records) {
        if (!a_intfc.OpenRecord(kRecordClone, kCloneVersion)) {
            logger::error(
                "ArmorClones: save failed | channel={} | source={:08X} | reason=openRecord",
                DisplaySlotLabel(record.channel),
                record.sourceArmorFormID
            );
            return;
        }

        const StoredCloneHeader header {
            .channel = std::to_underlying(record.channel),
            .sourceArmorFormID = record.sourceArmorFormID,
            .cloneArmorFormID = record.cloneArmorFormID,
            .sourceAddonCount = static_cast<std::uint32_t>(record.sourceAddonFormIDs.size()),
        };

        if (!a_intfc.WriteRecordData(header)) {
            logger::error("ArmorClones: save failed | source={:08X} | reason=writeHeader", record.sourceArmorFormID);
            return;
        }

        for (const auto sourceAddonFormID : record.sourceAddonFormIDs) {
            if (!a_intfc.WriteRecordData(sourceAddonFormID)) {
                logger::error(
                    "ArmorClones: save failed | source={:08X} | sourceAddon={:08X} | reason=writeSourceAddon",
                    record.sourceArmorFormID,
                    sourceAddonFormID
                );
                return;
            }
        }
    }
}

bool LoadRecord(const Serialization::RecordInfo a_recordInfo, SKSE::SerializationInterface& a_intfc) {
    if (a_recordInfo.type != kRecordClone) {
        return false;
    }

    if (a_recordInfo.version != kCloneVersion) {
        logger::warn("ArmorClones: load skipped | version={} | reason=unsupportedVersion", a_recordInfo.version);
        return true;
    }

    StoredCloneHeader storedHeader;
    if (a_intfc.ReadRecordData(storedHeader) != sizeof(storedHeader)) {
        logger::error("ArmorClones: load failed | reason=readHeader");
        return true;
    }

    CloneRecord record;
    record.channel = static_cast<DisplaySlot>(storedHeader.channel);
    if (record.channel != DisplaySlot::kRegular && record.channel != DisplaySlot::kBond) {
        logger::warn(
            "ArmorClones: record skipped | source={:08X} | channel={} | reason=invalidChannel",
            storedHeader.sourceArmorFormID,
            storedHeader.channel
        );
        return true;
    }

    bool valid = true;
    valid &= ResolveStoredFormID(a_intfc, storedHeader.sourceArmorFormID, record.sourceArmorFormID);
    valid &= ResolveStoredFormID(a_intfc, storedHeader.cloneArmorFormID, record.cloneArmorFormID);

    record.sourceAddonFormIDs.reserve(storedHeader.sourceAddonCount);
    for (std::uint32_t index = 0; index < storedHeader.sourceAddonCount; ++index) {
        RE::FormID storedSourceAddonFormID = 0;
        if (a_intfc.ReadRecordData(storedSourceAddonFormID) != sizeof(storedSourceAddonFormID)) {
            logger::error(
                "ArmorClones: load failed | source={:08X} | addonIndex={} | reason=readSourceAddon",
                storedHeader.sourceArmorFormID,
                index
            );
            return true;
        }

        RE::FormID sourceAddonFormID = 0;
        const auto sourceAddonResolved = ResolveStoredFormID(a_intfc, storedSourceAddonFormID, sourceAddonFormID);
        valid &= sourceAddonResolved;
        if (sourceAddonResolved) {
            record.sourceAddonFormIDs.push_back(sourceAddonFormID);
        }
    }

    if (!valid) {
        logger::warn(
            "ArmorClones: record skipped | source={:08X} | clone={:08X} | sourceAddons={} | reason=invalidResolvedData",
            storedHeader.sourceArmorFormID,
            storedHeader.cloneArmorFormID,
            storedHeader.sourceAddonCount
        );
        return true;
    }

    Register(record);
    return true;
}

void Revert() {
    std::scoped_lock lock(RegistryLock());
    auto& recordsBySource = RecordsBySource();
    auto& restoreRequiredSources = RestoreRequiredSources();
    recordsBySource.clear();
    restoreRequiredSources.clear();
}

void Revert(const DisplaySlot a_channel) {
    std::scoped_lock lock(RegistryLock());
    auto& recordsBySource = RecordsBySource();
    auto& restoreRequiredSources = RestoreRequiredSources();
    std::erase_if(recordsBySource, [a_channel](const auto& a_entry) {
        return a_entry.first.channel == a_channel;
    });
    std::erase_if(restoreRequiredSources, [a_channel](const auto& a_key) {
        return a_key.channel == a_channel;
    });
}
}
