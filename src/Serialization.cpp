#include "Serialization.h"

#include "EventBindings.h"
#include "RuntimeClones.h"
#include "RuntimeEquipment.h"
#include "Selection.h"

#include <array>
#include <utility>

namespace Serialization {
namespace {
    constexpr auto kSerializationID = MakeRecordType('L', 'H', 'R', 'S');
    constexpr auto kRecordState = MakeRecordType('S', 'T', 'A', 'T');
    constexpr std::uint32_t kRecordVersion = 1;

    struct SerializedCustomKeyHeader {
        RE::FormID enchantmentFormID {0};
        std::uint16_t charge {0};
        std::uint8_t removeOnUnequip {0};
        std::uint8_t pad {0};
        std::uint32_t displayNameLength {0};
    };

    template <class T>
    [[nodiscard]] bool WriteField(SKSE::SerializationInterface& a_intfc, const T& a_value) {
        return a_intfc.WriteRecordData(a_value);
    }

    [[nodiscard]] bool WriteState(SKSE::SerializationInterface& a_intfc, const Selection::State& a_state) {
        const auto kind = std::to_underlying(a_state.kind);
        if (!WriteField(a_intfc, kind) || !WriteField(a_intfc, a_state.sourceFormID)) {
            return false;
        }

        if (a_state.kind != Selection::Kind::kCustomEnchantment) {
            return true;
        }

        const auto& customKey = a_state.customKey;
        const auto header = SerializedCustomKeyHeader {
            .enchantmentFormID = customKey.enchantmentFormID,
            .charge = customKey.charge,
            .removeOnUnequip = customKey.removeOnUnequip ? std::uint8_t {1} : std::uint8_t {0},
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
        return WriteState(a_intfc, a_state.regular) && WriteState(a_intfc, a_state.bond);
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
        if (!ReadField(a_intfc, a_remaining, kind) || !ReadField(a_intfc, a_remaining, sourceFormID)) {
            return std::nullopt;
        }

        auto state = Selection::State {
            .kind = static_cast<Selection::Kind>(kind),
            .sourceFormID = sourceFormID,
        };

        if (state.kind == Selection::Kind::kCustomEnchantment) {
            SerializedCustomKeyHeader header;
            if (!ReadField(a_intfc, a_remaining, header) || header.displayNameLength > a_remaining) {
                DrainRecordData(a_intfc, a_remaining);
                return std::nullopt;
            }

            state.customKey = Inventory::CustomEnchantmentKey {
                .enchantmentFormID = header.enchantmentFormID,
                .charge = header.charge,
                .removeOnUnequip = header.removeOnUnequip != 0,
            };

            if (header.displayNameLength > 0) {
                state.customKey.playerDisplayName.resize(header.displayNameLength);
                const auto read = a_intfc.ReadRecordData(
                    state.customKey.playerDisplayName.data(),
                    header.displayNameLength
                );
                if (read != header.displayNameLength) {
                    DrainRecordData(a_intfc, a_remaining > read ? a_remaining - read : 0);
                    return std::nullopt;
                }
                a_remaining -= read;
            }
        }

        return state;
    }

    [[nodiscard]] std::optional<Selection::Snapshot> ReadSnapshot(
        SKSE::SerializationInterface& a_intfc,
        const std::uint32_t a_length
    ) {
        auto remaining = a_length;
        auto normal = ReadState(a_intfc, remaining);
        auto bond = normal ? ReadState(a_intfc, remaining) : std::nullopt;
        if (!normal || !bond) {
            DrainRecordData(a_intfc, remaining);
            return std::nullopt;
        }

        if (remaining != 0) {
            DrainRecordData(a_intfc, remaining);
            return std::nullopt;
        }

        return Selection::Snapshot {
            .regular = *normal,
            .bond = *bond,
        };
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

        const auto cloneKeys = Selection::GetCloneKeys();
        RuntimeClones::Save(*a_intfc, cloneKeys);
        EventBindings::Save(*a_intfc, cloneKeys);
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

            if (RuntimeClones::LoadRecord(recordInfo, *a_intfc)) {
                continue;
            }

            if (EventBindings::LoadRecord(recordInfo, *a_intfc)) {
                continue;
            }

            if (type != kRecordState) {
                logger::warn("Serialization: record skipped | type={:08X} | reason=unknownType", type);
                continue;
            }

            if (version != kRecordVersion) {
                logger::warn(
                    "Serialization: record skipped | version={} | length={} | reason=unsupportedStateVersion",
                    version,
                    length
                );
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
            RuntimeEquipment::RequestRefresh();
        });
    }

    void RevertCallback([[maybe_unused]] SKSE::SerializationInterface* a_intfc) {
        Selection::Revert();
        RuntimeClones::Revert();
        EventBindings::Revert();
        RuntimeEquipment::DiscardState();
        stl::add_task([] {
            RuntimeEquipment::RequestRefresh();
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
