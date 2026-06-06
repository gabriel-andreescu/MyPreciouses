#include "Serialization.h"

#include "Compatibility/Vanilla.h"
#include "Equipment/AssignmentActions.h"
#include "Equipment/AssignmentStore.h"
#include "Equipment/AutoEquip.h"
#include "Equipment/RaceSwitchRestore.h"
#include "Papyrus/ScriptEventMirror.h"
#include "VirtualSlots.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>
#include <vector>

namespace Serialization {
void DrainRecordData(SKSE::SerializationInterface& a_intfc, std::uint32_t a_remaining) {
    std::array<char, 256> buffer {};
    while (a_remaining > 0) {
        const auto toRead = std::min(a_remaining, static_cast<std::uint32_t>(buffer.size()));
        const auto read = a_intfc.ReadRecordData(buffer.data(), toRead);
        if (read == 0) {
            return;
        }
        a_remaining -= read;
    }
}

bool WriteString(SKSE::SerializationInterface& a_intfc, const std::string& a_value) {
    const auto length = static_cast<std::uint32_t>(a_value.size());
    if (!WriteField(a_intfc, length)) {
        return false;
    }

    return length == 0 || a_intfc.WriteRecordData(a_value.data(), length);
}

std::optional<std::string> ReadString(SKSE::SerializationInterface& a_intfc, std::uint32_t& a_remaining) {
    std::uint32_t length = 0;
    if (!ReadField(a_intfc, a_remaining, length) || length > a_remaining) {
        return std::nullopt;
    }

    std::string value(length, '\0');
    if (length == 0) {
        return value;
    }

    const auto read = a_intfc.ReadRecordData(value.data(), length);
    if (read != length) {
        return std::nullopt;
    }

    a_remaining -= read;
    return value;
}

namespace {
    constexpr auto kSerializationID = MakeRecordType('L', 'H', 'R', 'S');
    constexpr auto kRecordAssignments = MakeRecordType('S', 'T', 'A', 'T');
    constexpr auto kRecordRaceSwitchRestores = MakeRecordType('R', 'S', 'R', 'T');
    constexpr std::uint32_t kLegacyPlayerAssignmentRecordVersion = 4;
    constexpr std::uint32_t kAssignmentRecordVersion = 5;
    constexpr std::uint32_t kRaceSwitchRestoreRecordVersion = 1;
    constexpr RE::FormID kPlayerFormID = 0x14;

    struct SerializedCustomEnchantmentHeader {
        RE::FormID enchantmentFormID {0};
        std::uint16_t charge {0};
        std::uint8_t removeOnUnequip {0};
        std::uint8_t hasUniqueID {0};
        RE::FormID uniqueBaseID {0};
        std::uint16_t uniqueID {0};
        std::uint16_t pad {0};
        std::uint32_t displayNameLength {0};
    };

    [[nodiscard]] bool WriteAssignment(
        SKSE::SerializationInterface& a_intfc,
        const Core::Target a_target,
        const Core::Assignment& a_assignment
    ) {
        const auto targetIndex = Core::ToIndex(a_target);
        const auto kind = std::to_underlying(a_assignment.source.kind);
        if (!WriteField(a_intfc, targetIndex)
            || !WriteField(a_intfc, kind)
            || !WriteField(a_intfc, a_assignment.source.sourceFormID)
            || !WriteField(a_intfc, a_assignment.retainedEffectSourceFormID)) {
            return false;
        }

        if (a_assignment.source.kind != Core::ItemSourceKind::kCustomEnchantment) {
            return true;
        }

        const auto& customEnchantment = a_assignment.source.customEnchantment;
        const auto& extraUniqueID = a_assignment.source.extraUniqueID;
        const auto header = SerializedCustomEnchantmentHeader {
            .enchantmentFormID = customEnchantment.enchantmentFormID,
            .charge = customEnchantment.charge,
            .removeOnUnequip = customEnchantment.removeOnUnequip ? std::uint8_t {1} : std::uint8_t {0},
            .hasUniqueID = extraUniqueID ? std::uint8_t {1} : std::uint8_t {0},
            .uniqueBaseID = extraUniqueID ? extraUniqueID->baseID : RE::FormID {0},
            .uniqueID = extraUniqueID ? extraUniqueID->uniqueID : std::uint16_t {0},
            .displayNameLength = static_cast<std::uint32_t>(customEnchantment.playerDisplayName.size()),
        };

        if (!WriteField(a_intfc, header)) {
            return false;
        }

        return header.displayNameLength
               == 0
               || a_intfc.WriteRecordData(customEnchantment.playerDisplayName.data(), header.displayNameLength);
    }

    [[nodiscard]] bool WriteSnapshot(SKSE::SerializationInterface& a_intfc, const Core::TargetAssignments& a_snapshot) {
        const auto targetCount = static_cast<std::uint32_t>(Core::kVirtualTargets.size());
        if (!WriteField(a_intfc, targetCount)) {
            return false;
        }

        for (const auto target : Core::kVirtualTargets) {
            if (!WriteAssignment(a_intfc, target, a_snapshot.byTarget[Core::ToIndex(target)])) {
                return false;
            }
        }

        return true;
    }

    [[nodiscard]] bool WriteActorAssignments(
        SKSE::SerializationInterface& a_intfc,
        const std::vector<Core::ActorAssignments>& a_actorSnapshots
    ) {
        const auto actorCount = static_cast<std::uint32_t>(a_actorSnapshots.size());
        if (!WriteField(a_intfc, actorCount)) {
            return false;
        }

        for (const auto& actorSnapshot : a_actorSnapshots) {
            if (!WriteField(a_intfc, actorSnapshot.actor.referenceFormID)
                || !WriteSnapshot(a_intfc, actorSnapshot.assignments)) {
                return false;
            }
        }

        return true;
    }

    [[nodiscard]] bool WriteRaceSwitchRestores(
        SKSE::SerializationInterface& a_intfc,
        const std::vector<Equipment::RaceSwitchRestore::PendingRestore>& a_restores
    ) {
        const auto restoreCount = static_cast<std::uint32_t>(a_restores.size());
        if (!WriteField(a_intfc, restoreCount)) {
            return false;
        }

        for (const auto& restore : a_restores) {
            if (!WriteField(a_intfc, restore.actor.referenceFormID)
                || !WriteField(a_intfc, restore.raceFormID)
                || !WriteSnapshot(a_intfc, restore.assignments)) {
                return false;
            }
        }

        return true;
    }

    [[nodiscard]] std::optional<Core::Assignment> ReadAssignment(
        SKSE::SerializationInterface& a_intfc,
        std::uint32_t& a_remaining
    ) {
        std::uint32_t kind = 0;
        RE::FormID sourceFormID = 0;
        RE::FormID effectSourceFormID = 0;
        if (!ReadField(a_intfc, a_remaining, kind)
            || !ReadField(a_intfc, a_remaining, sourceFormID)
            || !ReadField(a_intfc, a_remaining, effectSourceFormID)) {
            return std::nullopt;
        }

        auto assignment = Core::Assignment {
            .source = Core::ItemSource {
                .kind = static_cast<Core::ItemSourceKind>(kind),
                .sourceFormID = sourceFormID,
            },
            .retainedEffectSourceFormID = effectSourceFormID,
        };

        if (assignment.source.kind != Core::ItemSourceKind::kCustomEnchantment) {
            return assignment;
        }

        SerializedCustomEnchantmentHeader header;
        if (!ReadField(a_intfc, a_remaining, header)) {
            return std::nullopt;
        }

        if (header.displayNameLength > a_remaining) {
            return std::nullopt;
        }

        assignment.source.customEnchantment = Core::CustomEnchantmentSignature {
            .enchantmentFormID = header.enchantmentFormID,
            .charge = header.charge,
            .removeOnUnequip = header.removeOnUnequip != 0,
        };

        if (header.hasUniqueID != 0) {
            assignment.source.extraUniqueID = Core::ExtraUniqueIDKey {
                .baseID = header.uniqueBaseID,
                .uniqueID = header.uniqueID,
            };
        }

        if (header.displayNameLength > 0) {
            assignment.source.customEnchantment.playerDisplayName.resize(header.displayNameLength);
            const auto read = a_intfc.ReadRecordData(
                assignment.source.customEnchantment.playerDisplayName.data(),
                header.displayNameLength
            );
            if (read != header.displayNameLength) {
                return std::nullopt;
            }
            a_remaining -= read;
        }

        return assignment;
    }

    [[nodiscard]] std::optional<Core::TargetAssignments> ReadSnapshot(
        SKSE::SerializationInterface& a_intfc,
        std::uint32_t& a_remaining
    ) {
        std::uint32_t targetCount = 0;
        if (!ReadField(a_intfc, a_remaining, targetCount) || targetCount != Core::kVirtualTargets.size()) {
            return std::nullopt;
        }

        Core::TargetAssignments snapshot;
        for (std::uint32_t index = 0; index < targetCount; ++index) {
            std::uint32_t storedTargetIndex = 0;
            if (!ReadField(a_intfc, a_remaining, storedTargetIndex)) {
                return std::nullopt;
            }

            const auto target = Core::FromIndex(storedTargetIndex);
            auto assignment = ReadAssignment(a_intfc, a_remaining);
            if (!target || !Core::IsVirtualTarget(*target) || !assignment) {
                return std::nullopt;
            }

            snapshot.byTarget[Core::ToIndex(*target)] = std::move(*assignment);
        }

        return snapshot;
    }

    [[nodiscard]] Core::ActorKey LegacyPlayerActorKey() {
        if (const auto actor = Core::GetPlayerActorKey()) {
            return actor;
        }

        return Core::ActorKey {
            .referenceFormID = kPlayerFormID,
        };
    }

    [[nodiscard]] std::optional<std::vector<Core::ActorAssignments>> ReadLegacyPlayerAssignments(
        SKSE::SerializationInterface& a_intfc,
        const std::uint32_t a_length
    ) {
        auto remaining = a_length;
        auto snapshot = ReadSnapshot(a_intfc, remaining);
        if (!snapshot) {
            DrainRecordData(a_intfc, remaining);
            return std::nullopt;
        }

        if (remaining != 0) {
            DrainRecordData(a_intfc, remaining);
            return std::nullopt;
        }

        return std::vector {
            Core::ActorAssignments {
                .actor = LegacyPlayerActorKey(),
                .assignments = std::move(*snapshot),
            },
        };
    }

    [[nodiscard]] std::optional<std::vector<Core::ActorAssignments>> ReadActorAssignments(
        SKSE::SerializationInterface& a_intfc,
        const std::uint32_t a_length
    ) {
        auto remaining = a_length;
        std::uint32_t actorCount = 0;
        if (!ReadField(a_intfc, remaining, actorCount)) {
            DrainRecordData(a_intfc, remaining);
            return std::nullopt;
        }

        std::vector<Core::ActorAssignments> snapshots;
        snapshots.reserve(actorCount);
        for (std::uint32_t index = 0; index < actorCount; ++index) {
            RE::FormID actorFormID = 0;
            if (!ReadField(a_intfc, remaining, actorFormID)) {
                DrainRecordData(a_intfc, remaining);
                return std::nullopt;
            }

            auto snapshot = ReadSnapshot(a_intfc, remaining);
            if (!snapshot) {
                DrainRecordData(a_intfc, remaining);
                return std::nullopt;
            }

            snapshots.push_back(
                Core::ActorAssignments {
                    .actor = Core::ActorKey {
                        .referenceFormID = actorFormID,
                    },
                    .assignments = std::move(*snapshot),
                }
            );
        }

        if (remaining != 0) {
            DrainRecordData(a_intfc, remaining);
            return std::nullopt;
        }

        return snapshots;
    }

    [[nodiscard]] std::optional<std::vector<Equipment::RaceSwitchRestore::PendingRestore>> ReadRaceSwitchRestores(
        SKSE::SerializationInterface& a_intfc,
        const std::uint32_t a_length
    ) {
        auto remaining = a_length;
        std::uint32_t restoreCount = 0;
        if (!ReadField(a_intfc, remaining, restoreCount)) {
            DrainRecordData(a_intfc, remaining);
            return std::nullopt;
        }

        std::vector<Equipment::RaceSwitchRestore::PendingRestore> restores;
        restores.reserve(restoreCount);
        for (std::uint32_t index = 0; index < restoreCount; ++index) {
            RE::FormID actorFormID = 0;
            RE::FormID raceFormID = 0;
            if (!ReadField(a_intfc, remaining, actorFormID) || !ReadField(a_intfc, remaining, raceFormID)) {
                DrainRecordData(a_intfc, remaining);
                return std::nullopt;
            }

            auto snapshot = ReadSnapshot(a_intfc, remaining);
            if (!snapshot) {
                DrainRecordData(a_intfc, remaining);
                return std::nullopt;
            }

            restores.push_back(
                Equipment::RaceSwitchRestore::PendingRestore {
                    .actor = Core::ActorKey {
                        .referenceFormID = actorFormID,
                    },
                    .raceFormID = raceFormID,
                    .assignments = std::move(*snapshot),
                }
            );
        }

        if (remaining != 0) {
            DrainRecordData(a_intfc, remaining);
            return std::nullopt;
        }

        return restores;
    }

    [[nodiscard]] std::optional<std::vector<Core::ActorAssignments>> ReadAssignments(
        SKSE::SerializationInterface& a_intfc,
        const std::uint32_t a_version,
        const std::uint32_t a_length
    ) {
        if (a_version == kLegacyPlayerAssignmentRecordVersion) {
            return ReadLegacyPlayerAssignments(a_intfc, a_length);
        }

        if (a_version == kAssignmentRecordVersion) {
            return ReadActorAssignments(a_intfc, a_length);
        }

        return std::nullopt;
    }

    [[nodiscard]] RE::FormID ResolveFormID(
        SKSE::SerializationInterface& a_intfc,
        const RE::FormID a_formID,
        const std::string_view a_label
    ) {
        if (a_formID == 0) {
            return 0;
        }

        RE::FormID resolvedFormID = 0;
        if (!a_intfc.ResolveFormID(a_formID, resolvedFormID)) {
            logger::warn("Serialization: virtual ring resolve failed | field={} | form={:08X}", a_label, a_formID);
            return 0;
        }
        return resolvedFormID;
    }

    [[nodiscard]] std::optional<Core::Assignment> ResolveAssignment(
        const Core::ActorKey a_actor,
        const Core::Target a_target,
        const Core::Assignment& a_savedAssignment,
        SKSE::SerializationInterface& a_intfc
    ) {
        if (!a_savedAssignment.source.IsAssigned()) {
            return std::nullopt;
        }

        const auto sourceFormID = ResolveFormID(a_intfc, a_savedAssignment.source.sourceFormID, "source form"sv);
        if (sourceFormID == 0) {
            return std::nullopt;
        }

        const auto retainedEffectSourceFormID = ResolveFormID(
            a_intfc,
            a_savedAssignment.retainedEffectSourceFormID,
            "effect source form"sv
        );
        if (a_savedAssignment.retainedEffectSourceFormID != 0 && retainedEffectSourceFormID == 0) {
            logger::warn(
                "Serialization: virtual assignment cleared | actor={:08X} | target={} | source={:08X} | effectSource={:08X} | reason=effectSourceResolveFailed",
                a_actor.referenceFormID,
                Core::TargetName(a_target),
                sourceFormID,
                a_savedAssignment.retainedEffectSourceFormID
            );
            return std::nullopt;
        }

        if (a_savedAssignment.source.kind == Core::ItemSourceKind::kFormOnly) {
            return Core::Assignment {
                .source = Core::ItemSource {
                    .kind = Core::ItemSourceKind::kFormOnly,
                    .sourceFormID = sourceFormID,
                },
                .retainedEffectSourceFormID = retainedEffectSourceFormID,
            };
        }

        if (a_savedAssignment.source.kind != Core::ItemSourceKind::kCustomEnchantment) {
            logger::warn(
                "Serialization: virtual assignment cleared | actor={:08X} | target={} | source={:08X} | kind={} | reason=unsupportedKind",
                a_actor.referenceFormID,
                Core::TargetName(a_target),
                sourceFormID,
                std::to_underlying(a_savedAssignment.source.kind)
            );
            return std::nullopt;
        }

        auto customEnchantment = a_savedAssignment.source.customEnchantment;
        customEnchantment
            .enchantmentFormID = ResolveFormID(a_intfc, customEnchantment.enchantmentFormID, "custom enchantment"sv);
        if (customEnchantment.enchantmentFormID
            == 0
            || !RE::TESForm::LookupByID<RE::EnchantmentItem>(customEnchantment.enchantmentFormID)) {
            logger::warn(
                "Serialization: custom virtual assignment cleared | actor={:08X} | target={} | source={:08X} | enchantment={:08X} | reason=enchantmentMissing",
                a_actor.referenceFormID,
                Core::TargetName(a_target),
                sourceFormID,
                a_savedAssignment.source.customEnchantment.enchantmentFormID
            );
            return std::nullopt;
        }

        auto extraUniqueID = a_savedAssignment.source.extraUniqueID;
        if (extraUniqueID) {
            extraUniqueID->baseID = ResolveFormID(a_intfc, extraUniqueID->baseID, "custom unique base"sv);
            if (!extraUniqueID->IsValid()) {
                logger::warn(
                    "Serialization: custom virtual assignment cleared | actor={:08X} | target={} | source={:08X} | reason=uniqueIDResolveFailed",
                    a_actor.referenceFormID,
                    Core::TargetName(a_target),
                    sourceFormID
                );
                return std::nullopt;
            }
        }

        return Core::Assignment {
            .source = Core::ItemSource {
                .kind = Core::ItemSourceKind::kCustomEnchantment,
                .sourceFormID = sourceFormID,
                .customEnchantment = std::move(customEnchantment),
                .extraUniqueID = extraUniqueID,
            },
            .retainedEffectSourceFormID = retainedEffectSourceFormID,
        };
    }

    [[nodiscard]] std::vector<Core::ActorAssignments> ResolveLoadedAssignments(
        const std::vector<Core::ActorAssignments>& a_savedSnapshots,
        SKSE::SerializationInterface& a_intfc
    ) {
        std::vector<Core::ActorAssignments> resolvedSnapshots;
        resolvedSnapshots.reserve(a_savedSnapshots.size());
        for (const auto& actorSnapshot : a_savedSnapshots) {
            const auto actorFormID = ResolveFormID(a_intfc, actorSnapshot.actor.referenceFormID, "actor"sv);
            if (actorFormID == 0) {
                continue;
            }

            const auto actor = Core::ActorKey {
                .referenceFormID = actorFormID,
            };
            Core::TargetAssignments resolvedSnapshot;
            for (const auto target : Core::kVirtualTargets) {
                const auto& savedAssignment = actorSnapshot.assignments.byTarget[Core::ToIndex(target)];
                auto resolvedAssignment = ResolveAssignment(actor, target, savedAssignment, a_intfc);
                if (resolvedAssignment) {
                    resolvedSnapshot.byTarget[Core::ToIndex(target)] = std::move(*resolvedAssignment);
                }
            }

            resolvedSnapshots.push_back(
                Core::ActorAssignments {
                    .actor = actor,
                    .assignments = std::move(resolvedSnapshot),
                }
            );
        }

        return resolvedSnapshots;
    }

    [[nodiscard]] std::vector<Equipment::RaceSwitchRestore::PendingRestore> ResolveLoadedRaceSwitchRestores(
        const std::vector<Equipment::RaceSwitchRestore::PendingRestore>& a_savedRestores,
        SKSE::SerializationInterface& a_intfc
    ) {
        std::vector<Equipment::RaceSwitchRestore::PendingRestore> resolvedRestores;
        resolvedRestores.reserve(a_savedRestores.size());
        for (const auto& savedRestore : a_savedRestores) {
            const auto actorFormID = ResolveFormID(a_intfc, savedRestore.actor.referenceFormID, "race switch actor"sv);
            const auto raceFormID = ResolveFormID(a_intfc, savedRestore.raceFormID, "race switch race"sv);
            if (actorFormID == 0 || raceFormID == 0) {
                continue;
            }

            const auto actor = Core::ActorKey {
                .referenceFormID = actorFormID,
            };
            Core::TargetAssignments resolvedSnapshot;
            for (const auto target : Core::kVirtualTargets) {
                const auto& savedAssignment = savedRestore.assignments.byTarget[Core::ToIndex(target)];
                auto resolvedAssignment = ResolveAssignment(actor, target, savedAssignment, a_intfc);
                if (resolvedAssignment) {
                    resolvedSnapshot.byTarget[Core::ToIndex(target)] = std::move(*resolvedAssignment);
                }
            }

            resolvedRestores.push_back(
                Equipment::RaceSwitchRestore::PendingRestore {
                    .actor = actor,
                    .raceFormID = raceFormID,
                    .assignments = std::move(resolvedSnapshot),
                }
            );
        }

        return resolvedRestores;
    }

    [[nodiscard]] bool SameBindingRetentionKey(
        const Papyrus::ScriptEventMirror::BindingRetentionKey& a_lhs,
        const Papyrus::ScriptEventMirror::BindingRetentionKey& a_rhs
    ) {
        return a_lhs.sourceFormID == a_rhs.sourceFormID && a_lhs.effectSourceFormID == a_rhs.effectSourceFormID;
    }

    void AddBindingRetentionKey(
        std::vector<Papyrus::ScriptEventMirror::BindingRetentionKey>& a_keys,
        Papyrus::ScriptEventMirror::BindingRetentionKey a_key
    ) {
        if (a_key.sourceFormID == 0 || a_key.effectSourceFormID == 0) {
            return;
        }

        const auto duplicate = std::ranges::any_of(a_keys, [&](const auto& a_existing) {
            return SameBindingRetentionKey(a_existing, a_key);
        });
        if (!duplicate) {
            a_keys.push_back(a_key);
        }
    }

    void AddAssignmentBindingRetentionKeys(
        std::vector<Papyrus::ScriptEventMirror::BindingRetentionKey>& a_keys,
        const Core::TargetAssignments& a_assignments
    ) {
        for (const auto target : Core::kVirtualTargets) {
            const auto& assignment = a_assignments.byTarget[Core::ToIndex(target)];
            if (!assignment.IsAssigned()) {
                continue;
            }

            AddBindingRetentionKey(
                a_keys,
                Papyrus::ScriptEventMirror::BindingRetentionKey {
                    .sourceFormID = assignment.source.sourceFormID,
                    .effectSourceFormID = assignment.retainedEffectSourceFormID,
                }
            );
        }
    }

    [[nodiscard]] std::vector<Papyrus::ScriptEventMirror::BindingRetentionKey> GetSaveBindingRetentionKeys(
        const std::vector<Core::ActorAssignments>& a_snapshots,
        const std::vector<Equipment::RaceSwitchRestore::PendingRestore>& a_raceSwitchRestores
    ) {
        auto keys = VirtualSlots::GetActiveBindingRetentionKeys();
        for (const auto& actorState : a_snapshots) {
            AddAssignmentBindingRetentionKeys(keys, actorState.assignments);
        }

        for (const auto& restore : a_raceSwitchRestores) {
            AddAssignmentBindingRetentionKeys(keys, restore.assignments);
        }

        return keys;
    }

    void QueueAssignmentRefresh() {
        stl::add_task([] {
            const auto loadRefreshOptions = VirtualSlots::RefreshOptions {
                .preserveLoadedEffects = true,
            };
            const auto snapshots = Equipment::AssignmentStore::GetAllSnapshots();
            if (snapshots.empty()) {
                VirtualSlots::RequestRefresh(Core::GetPlayerActorKey(), loadRefreshOptions);
                return;
            }

            for (const auto& snapshot : snapshots) {
                if (snapshot.actor == Core::GetPlayerActorKey()) {
                    VirtualSlots::RequestRefresh(snapshot.actor, loadRefreshOptions);
                } else {
                    Equipment::AutoEquip::QueueRefresh(snapshot.actor, Equipment::AutoEquip::RefreshReason::kLoad);
                }
            }
        });
    }

    void SaveCallback(SKSE::SerializationInterface* a_intfc) {
        if (!a_intfc) {
            return;
        }

        const auto snapshots = Equipment::AssignmentStore::GetAllSnapshots();
        const auto raceSwitchRestores = Equipment::RaceSwitchRestore::GetPendingRestores();
        if (!a_intfc->OpenRecord(kRecordAssignments, kAssignmentRecordVersion)) {
            logger::error("Serialization: save failed | record=STAT | reason=openRecord");
            return;
        }

        if (!WriteActorAssignments(*a_intfc, snapshots)) {
            logger::error("Serialization: save failed | record=STAT | reason=writeRecord");
        }

        if (!raceSwitchRestores.empty()) {
            if (!a_intfc->OpenRecord(kRecordRaceSwitchRestores, kRaceSwitchRestoreRecordVersion)) {
                logger::error("Serialization: save failed | record=RSRT | reason=openRecord");
            } else if (!WriteRaceSwitchRestores(*a_intfc, raceSwitchRestores)) {
                logger::error("Serialization: save failed | record=RSRT | reason=writeRecord");
            }
        }

        Papyrus::ScriptEventMirror::SaveBindings(*a_intfc, GetSaveBindingRetentionKeys(snapshots, raceSwitchRestores));
        Compatibility::Vanilla::Save(*a_intfc);
    }

    void LoadCallback(SKSE::SerializationInterface* a_intfc) {
        if (!a_intfc) {
            return;
        }

        std::uint32_t type = 0;
        std::uint32_t version = 0;
        std::uint32_t length = 0;

        while (a_intfc->GetNextRecordInfo(type, version, length)) {
            const auto recordInfo = RecordInfo {
                .type = type,
                .version = version,
                .length = length,
            };

            if (Papyrus::ScriptEventMirror::TryLoadBindingRecord(recordInfo, *a_intfc)) {
                continue;
            }

            if (Compatibility::Vanilla::TryLoadRecord(recordInfo, *a_intfc)) {
                continue;
            }

            if (type == kRecordRaceSwitchRestores) {
                if (version != kRaceSwitchRestoreRecordVersion) {
                    logger::warn(
                        "Serialization: record skipped | version={} | length={} | reason=unsupportedRaceSwitchRestoreVersion",
                        version,
                        length
                    );
                    DrainRecordData(*a_intfc, length);
                    continue;
                }

                auto restores = ReadRaceSwitchRestores(*a_intfc, length);
                if (!restores) {
                    logger::error("Serialization: load failed | record=RSRT | reason=readRecord");
                    continue;
                }

                Equipment::RaceSwitchRestore::ReplacePendingRestores(
                    ResolveLoadedRaceSwitchRestores(*restores, *a_intfc)
                );
                continue;
            }

            if (type != kRecordAssignments) {
                logger::warn(
                    "Serialization: record skipped | type={:08X} | length={} | reason=unknownType",
                    type,
                    length
                );
                DrainRecordData(*a_intfc, length);
                continue;
            }

            if (version != kAssignmentRecordVersion && version != kLegacyPlayerAssignmentRecordVersion) {
                logger::warn(
                    "Serialization: record skipped | version={} | length={} | reason=unsupportedAssignmentVersion",
                    version,
                    length
                );
                DrainRecordData(*a_intfc, length);
                continue;
            }

            auto snapshots = ReadAssignments(*a_intfc, version, length);
            if (!snapshots) {
                logger::error("Serialization: load failed | record=STAT | reason=readRecord");
                continue;
            }

            Equipment::AssignmentStore::ReplaceAll(ResolveLoadedAssignments(*snapshots, *a_intfc));
            static_cast<void>(Equipment::ClearDisabledVirtualSlotAssignments(Equipment::RefreshMode::kNone));
        }

        QueueAssignmentRefresh();
    }

    void RevertCallback([[maybe_unused]] SKSE::SerializationInterface* a_intfc) {
        Equipment::AssignmentStore::Revert();
        Equipment::AutoEquip::Revert();
        Equipment::RaceSwitchRestore::Revert();
        Papyrus::ScriptEventMirror::RevertBindings();
        VirtualSlots::Revert();
        QueueAssignmentRefresh();
    }
}

void Install() {
    const auto* serialization = SKSE::GetSerializationInterface();
    if (!serialization) {
        logger::critical("Serialization: callbacks skipped | reason=noInterface");
        return;
    }

    serialization->SetUniqueID(kSerializationID);
    serialization->SetSaveCallback(SaveCallback);
    serialization->SetLoadCallback(LoadCallback);
    serialization->SetRevertCallback(RevertCallback);
    logger::info("Serialization: registered callbacks");
}
}
