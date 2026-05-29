#include "Serialization.h"

#include "EventBindings.h"
#include "Selection.h"
#include "VanillaCompatibility.h"
#include "VirtualRings.h"

#include <array>
#include <utility>

namespace Serialization {
namespace {
    constexpr auto kSerializationID = MakeRecordType('L', 'H', 'R', 'S');
    constexpr auto kRecordState = MakeRecordType('S', 'T', 'A', 'T');
    constexpr std::uint32_t kRecordVersion = 4;

    struct SerializedCustomKeyHeader {
        RE::FormID enchantmentFormID {0};
        std::uint16_t charge {0};
        std::uint8_t removeOnUnequip {0};
        std::uint8_t hasIdentity {0};
        RE::FormID uniqueBaseID {0};
        std::uint16_t uniqueID {0};
        std::uint16_t pad {0};
        std::uint32_t displayNameLength {0};
    };

    template <class T>
    [[nodiscard]] bool WriteField(SKSE::SerializationInterface& a_intfc, const T& a_value) {
        return a_intfc.WriteRecordData(a_value);
    }

    [[nodiscard]] bool WriteState(
        SKSE::SerializationInterface& a_intfc,
        const RingTarget a_target,
        const Selection::State& a_state
    ) {
        const auto targetIndex = ToIndex(a_target);
        const auto kind = std::to_underlying(a_state.kind);
        if (!WriteField(a_intfc, targetIndex)
            || !WriteField(a_intfc, kind)
            || !WriteField(a_intfc, a_state.sourceFormID)
            || !WriteField(a_intfc, a_state.restoredEffectSourceFormID)) {
            return false;
        }

        if (a_state.kind != Selection::Kind::kCustomEnchantment) {
            return true;
        }

        const auto& customKey = a_state.customKey;
        const auto& customIdentity = a_state.customIdentity;
        const auto header = SerializedCustomKeyHeader {
            .enchantmentFormID = customKey.enchantmentFormID,
            .charge = customKey.charge,
            .removeOnUnequip = customKey.removeOnUnequip ? std::uint8_t {1} : std::uint8_t {0},
            .hasIdentity = customIdentity ? std::uint8_t {1} : std::uint8_t {0},
            .uniqueBaseID = customIdentity ? customIdentity->baseID : RE::FormID {0},
            .uniqueID = customIdentity ? customIdentity->uniqueID : std::uint16_t {0},
            .displayNameLength = static_cast<std::uint32_t>(customKey.playerDisplayName.size()),
        };

        if (!WriteField(a_intfc, header)) {
            return false;
        }

        return header.displayNameLength
               == 0
               || a_intfc.WriteRecordData(customKey.playerDisplayName.data(), header.displayNameLength);
    }

    [[nodiscard]] bool WriteSnapshot(SKSE::SerializationInterface& a_intfc, const Selection::Snapshot& a_state) {
        const auto targetCount = static_cast<std::uint32_t>(kVirtualRingTargets.size());
        if (!WriteField(a_intfc, targetCount)) {
            return false;
        }

        for (const auto target : kVirtualRingTargets) {
            if (!WriteState(a_intfc, target, a_state.targets[ToIndex(target)])) {
                return false;
            }
        }

        return true;
    }

    template <class T>
    [[nodiscard]] bool ReadField(SKSE::SerializationInterface& a_intfc, std::uint32_t& a_remaining, T& a_value) {
        if (a_remaining < sizeof(T)) {
            return false;
        }

        const auto read = a_intfc.ReadRecordData(a_value);
        if (read != sizeof(T)) {
            return false;
        }

        a_remaining -= read;
        return true;
    }

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

    [[nodiscard]] std::optional<Selection::State> ReadState(
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

        auto state = Selection::State {
            .kind = static_cast<Selection::Kind>(kind),
            .sourceFormID = sourceFormID,
            .restoredEffectSourceFormID = effectSourceFormID,
        };

        if (state.kind != Selection::Kind::kCustomEnchantment) {
            return state;
        }

        SerializedCustomKeyHeader header;
        if (!ReadField(a_intfc, a_remaining, header)) {
            return std::nullopt;
        }

        if (header.displayNameLength > a_remaining) {
            return std::nullopt;
        }

        state.customKey = Inventory::CustomEnchantmentKey {
            .enchantmentFormID = header.enchantmentFormID,
            .charge = header.charge,
            .removeOnUnequip = header.removeOnUnequip != 0,
        };

        if (header.hasIdentity != 0) {
            state.customIdentity = Inventory::ExtraListIdentity {
                .baseID = header.uniqueBaseID,
                .uniqueID = header.uniqueID,
            };
        }

        if (header.displayNameLength > 0) {
            state.customKey.playerDisplayName.resize(header.displayNameLength);
            const auto read = a_intfc.ReadRecordData(
                state.customKey.playerDisplayName.data(),
                header.displayNameLength
            );
            if (read != header.displayNameLength) {
                return std::nullopt;
            }
            a_remaining -= read;
        }

        return state;
    }

    [[nodiscard]] std::optional<Selection::Snapshot> ReadSnapshot(
        SKSE::SerializationInterface& a_intfc,
        const std::uint32_t a_length
    ) {
        auto remaining = a_length;
        std::uint32_t targetCount = 0;
        if (!ReadField(a_intfc, remaining, targetCount) || targetCount != kVirtualRingTargets.size()) {
            DrainRecordData(a_intfc, remaining);
            return std::nullopt;
        }

        Selection::Snapshot snapshot;
        for (std::uint32_t index = 0; index < targetCount; ++index) {
            std::uint32_t storedTargetIndex = 0;
            if (!ReadField(a_intfc, remaining, storedTargetIndex)) {
                DrainRecordData(a_intfc, remaining);
                return std::nullopt;
            }

            const auto target = FromIndex(storedTargetIndex);
            auto state = ReadState(a_intfc, remaining);
            if (!target || !IsVirtualRingTarget(*target) || !state) {
                DrainRecordData(a_intfc, remaining);
                return std::nullopt;
            }

            snapshot.targets[ToIndex(*target)] = std::move(*state);
        }

        if (remaining != 0) {
            DrainRecordData(a_intfc, remaining);
            return std::nullopt;
        }

        return snapshot;
    }

    void SaveCallback(SKSE::SerializationInterface* a_intfc) {
        if (!a_intfc) {
            return;
        }

        const auto state = Selection::GetSnapshot();
        if (!a_intfc->OpenRecord(kRecordState, kRecordVersion)) {
            logger::error("Serialization: save failed | record=state | reason=openRecord");
            return;
        }

        if (!WriteSnapshot(*a_intfc, state)) {
            logger::error("Serialization: save failed | record=state | reason=writeRecord");
        }

        EventBindings::Save(*a_intfc, VirtualRings::GetEventBindingSources());
        VanillaCompatibility::Save(*a_intfc);
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

            if (EventBindings::LoadRecord(recordInfo, *a_intfc)) {
                continue;
            }

            if (VanillaCompatibility::LoadRecord(recordInfo, *a_intfc)) {
                continue;
            }

            if (type != kRecordState) {
                logger::warn(
                    "Serialization: record skipped | type={:08X} | length={} | reason=unknownType",
                    type,
                    length
                );
                DrainRecordData(*a_intfc, length);
                continue;
            }

            if (version != kRecordVersion) {
                logger::warn(
                    "Serialization: record skipped | version={} | length={} | reason=unsupportedStateVersion",
                    version,
                    length
                );
                DrainRecordData(*a_intfc, length);
                continue;
            }

            auto state = ReadSnapshot(*a_intfc, length);
            if (!state) {
                logger::error("Serialization: load failed | record=state | reason=readRecord");
                continue;
            }

            Selection::Load(*state, *a_intfc);
        }

        stl::add_task([] {
            VirtualRings::RequestRefresh();
        });
    }

    void RevertCallback([[maybe_unused]] SKSE::SerializationInterface* a_intfc) {
        Selection::Revert();
        EventBindings::Revert();
        VirtualRings::Revert();
        stl::add_task([] {
            VirtualRings::RequestRefresh();
        });
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
