#include "SourceModelFootprints.h"

#include "ModelPaths.h"

#include <algorithm>
#include <format>
#include <limits>
#include <mutex>
#include <ranges>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace SourceModelFootprints {
namespace {
    constexpr auto kLeftFingerNodePrefix = "NPC L Finger"sv;
    constexpr auto kRightFingerNodePrefix = "NPC R Finger"sv;
    constexpr auto kLeftFingerCodePrefix = " [LF"sv;
    constexpr auto kRightFingerCodePrefix = " [RF"sv;
    constexpr auto kNodeCodeSuffix = "]"sv;

    struct FingerBone {
        Core::Hand hand {Core::Hand::kRight};
        Core::Finger finger {Core::Finger::kIndex};
        std::uint8_t segment {0};

        [[nodiscard]] bool operator==(const FingerBone&) const = default;
    };

    std::mutex g_cacheLock;

    [[nodiscard]] std::unordered_map<RE::FormID, Core::FingerMask>& SourceFingerMasks() {
        static auto* masks = new std::unordered_map<RE::FormID, Core::FingerMask>();
        return *masks;
    }

    [[nodiscard]] bool IsAsciiDigit(const char a_value) {
        return a_value >= '0' && a_value <= '9';
    }

    [[nodiscard]] bool IsNonEmptyAsciiNumber(const std::string_view a_value) {
        return !a_value.empty() && std::ranges::all_of(a_value, IsAsciiDigit);
    }

    [[nodiscard]] std::optional<std::uint8_t> ParseAsciiByte(const std::string_view a_value) {
        if (!IsNonEmptyAsciiNumber(a_value)) {
            return std::nullopt;
        }

        auto result = std::uint32_t {0};
        for (const auto value : a_value) {
            result = (result * 10) + static_cast<std::uint32_t>(value - '0');
            if (result > std::numeric_limits<std::uint8_t>::max()) {
                return std::nullopt;
            }
        }

        return static_cast<std::uint8_t>(result);
    }

    [[nodiscard]] std::optional<FingerBone> ParseFingerBoneNameForHand(
        std::string_view a_name,
        const Core::Hand a_hand,
        const std::string_view a_nodePrefix,
        const std::string_view a_codePrefix
    ) {
        if (!a_name.starts_with(a_nodePrefix)) {
            return std::nullopt;
        }

        const auto remainingName = a_name.substr(a_nodePrefix.size());
        const auto codeStart = remainingName.find(a_codePrefix);
        if (codeStart == std::string_view::npos) {
            return std::nullopt;
        }

        const auto fingerNumber = remainingName.substr(0, codeStart);
        if (fingerNumber.size() < 2 || !IsNonEmptyAsciiNumber(fingerNumber)) {
            return std::nullopt;
        }

        const auto code = remainingName.substr(codeStart);
        const auto minimumCodeSize = a_codePrefix.size() + kNodeCodeSuffix.size();
        if (code.size() < minimumCodeSize || !code.ends_with(kNodeCodeSuffix)) {
            return std::nullopt;
        }

        const auto codeNumberLength = code.size() - minimumCodeSize;
        if (codeNumberLength != fingerNumber.size()) {
            return std::nullopt;
        }

        const auto codeNumber = code.substr(a_codePrefix.size(), codeNumberLength);
        if (codeNumber != fingerNumber) {
            return std::nullopt;
        }

        const auto familyNumber = ParseAsciiByte(fingerNumber.substr(0, fingerNumber.size() - 1));
        if (!familyNumber || *familyNumber > std::to_underlying(Core::Finger::kPinky)) {
            return std::nullopt;
        }

        const auto segmentChar = fingerNumber.back();
        if (!IsAsciiDigit(segmentChar)) {
            return std::nullopt;
        }

        return FingerBone {
            .hand = a_hand,
            .finger = static_cast<Core::Finger>(*familyNumber),
            .segment = static_cast<std::uint8_t>(segmentChar - '0'),
        };
    }

    [[nodiscard]] std::optional<FingerBone> ParseFingerBoneName(const std::string_view a_name) {
        if (const auto bone = ParseFingerBoneNameForHand(
                a_name,
                Core::Hand::kLeft,
                kLeftFingerNodePrefix,
                kLeftFingerCodePrefix
            )) {
            return bone;
        }

        return ParseFingerBoneNameForHand(a_name, Core::Hand::kRight, kRightFingerNodePrefix, kRightFingerCodePrefix);
    }

    [[nodiscard]] constexpr char GetFingerNodeSideCode(const Core::Hand a_hand) {
        return a_hand == Core::Hand::kLeft ? 'L' : 'R';
    }

    [[nodiscard]] std::string MakeFingerBoneName(const FingerBone& a_bone) {
        const auto side = GetFingerNodeSideCode(a_bone.hand);
        const auto family = static_cast<unsigned>(std::to_underlying(a_bone.finger));
        const auto segment = static_cast<unsigned>(a_bone.segment);
        return std::format("NPC {} Finger{}{} [{}F{}{}]", side, family, segment, side, family, segment);
    }

    void CollectFingerMaskFromSkin(const RE::NiSkinInstance& a_skin, Core::FingerMask& a_fingerMask) {
        const auto boneCount = a_skin.skinData ? a_skin.skinData->bones : a_skin.numMatrices;
        for (std::uint32_t index = 0; index < boneCount; ++index) {
            const auto* bone = a_skin.bones ? a_skin.bones[index] : nullptr;
            const auto* boneName = bone ? bone->name.c_str() : nullptr;
            if (!boneName || boneName[0] == '\0') {
                continue;
            }

            if (const auto parsedBone = ParseFingerBoneName(boneName)) {
                a_fingerMask.Add(parsedBone->finger);
            }
        }
    }

    void CollectFingerMaskFromModel(RE::NiAVObject& a_root, Core::FingerMask& a_fingerMask) {
        RE::BSVisit::TraverseScenegraphObjects(std::addressof(a_root), [&](RE::NiAVObject* a_object) {
            const auto* nodeName = a_object ? a_object->name.c_str() : nullptr;
            if (nodeName && nodeName[0] != '\0') {
                if (const auto parsedBone = ParseFingerBoneName(nodeName)) {
                    a_fingerMask.Add(parsedBone->finger);
                }
            }

            return RE::BSVisit::BSVisitControl::kContinue;
        });

        RE::BSVisit::TraverseScenegraphGeometries(std::addressof(a_root), [&](RE::BSGeometry* a_geometry) {
            if (!a_geometry) {
                return RE::BSVisit::BSVisitControl::kContinue;
            }

            auto& geometryData = a_geometry->GetGeometryRuntimeData();
            if (auto* skin = geometryData.skinInstance.get()) {
                CollectFingerMaskFromSkin(*skin, a_fingerMask);
            }

            return RE::BSVisit::BSVisitControl::kContinue;
        });
    }

    [[nodiscard]] std::vector<std::string> GetSourceModelPaths(const RE::TESObjectARMO& a_ring) {
        std::vector<std::string> paths;
        for (const auto* addon : a_ring.armorAddons) {
            if (!addon || addon->GetSlotMask() != RE::BGSBipedObjectForm::BipedObjectSlot::kRing) {
                continue;
            }

            for (const auto sex : {RE::SEXES::kMale, RE::SEXES::kFemale}) {
                const auto sexIndex = std::to_underlying(sex);
                ModelPaths::AddUnique(paths, addon->bipedModels[sexIndex]);
                ModelPaths::AddUnique(paths, addon->bipedModel1stPersons[sexIndex]);
            }
        }

        return paths;
    }

    void MergeModelFingerMask(const std::string& a_path, Core::FingerMask& a_fingerMask) {
        RE::NiPointer<RE::NiNode> sourceRoot;
        const RE::BSModelDB::DBTraits::ArgsType args;
        const auto loadResult = RE::BSModelDB::Demand(a_path.c_str(), sourceRoot, args);
        if (loadResult != RE::BSResource::ErrorCode::kNone || !sourceRoot) {
            logger::debug(
                "SourceModelFootprints: source load failed | path='{}' | error={}",
                a_path,
                std::to_underlying(loadResult)
            );
            return;
        }

        CollectFingerMaskFromModel(*sourceRoot, a_fingerMask);
    }

    [[nodiscard]] Core::FingerMask DetectSourceFingerMask(const RE::TESObjectARMO& a_ring) {
        Core::FingerMask fingerMask;
        for (const auto& path : GetSourceModelPaths(a_ring)) {
            MergeModelFingerMask(path, fingerMask);
        }

        return fingerMask;
    }

    [[nodiscard]] std::uint8_t RetargetSegment(const FingerBone& a_source, const Core::Finger a_targetFinger) {
        if (a_targetFinger != Core::Finger::kThumb || a_source.finger == Core::Finger::kThumb) {
            return a_source.segment;
        }

        constexpr auto kLastThumbSegment = std::uint8_t {2};
        const auto segment = static_cast<std::uint8_t>(a_source.segment + 1);
        return std::min(segment, kLastThumbSegment);
    }

    [[nodiscard]] FingerBone RetargetFingerBone(
        const FingerBone& a_source,
        const Core::Target a_target,
        const Core::FingerMask& a_sourceFingerMask
    ) {
        const auto targetFinger = a_sourceFingerMask.IsMultiFinger() ? a_source.finger : a_target.finger;
        return FingerBone {
            .hand = a_target.hand,
            .finger = targetFinger,
            .segment = RetargetSegment(a_source, targetFinger),
        };
    }
}

std::optional<std::string> RetargetFingerBoneName(
    const std::string_view a_name,
    const Core::Target a_target,
    const Core::FingerMask& a_sourceFingerMask
) {
    const auto sourceBone = ParseFingerBoneName(a_name);
    if (!sourceBone) {
        return std::nullopt;
    }

    return MakeFingerBoneName(RetargetFingerBone(*sourceBone, a_target, a_sourceFingerMask));
}

Core::FingerMask GetSourceFingerMask(const RE::TESObjectARMO& a_ring) {
    const auto formID = a_ring.GetFormID();
    {
        std::scoped_lock lock(g_cacheLock);
        const auto& sourceFingerMasks = SourceFingerMasks();
        if (const auto cached = sourceFingerMasks.find(formID); cached != sourceFingerMasks.end()) {
            return cached->second;
        }
    }

    const auto fingerMask = DetectSourceFingerMask(a_ring);
    std::scoped_lock lock(g_cacheLock);
    return SourceFingerMasks().try_emplace(formID, fingerMask).first->second;
}

Core::TargetMask GetProjectedTargets(const RE::TESObjectARMO& a_ring, const Core::Target a_target) {
    return GetProjectedTargets(GetSourceFingerMask(a_ring), a_target);
}

Core::TargetMask GetProjectedTargets(const Core::FingerMask& a_sourceFingerMask, const Core::Target a_target) {
    Core::TargetMask mask;
    if (!a_sourceFingerMask.IsMultiFinger()) {
        mask.Add(a_target);
        return mask;
    }

    for (const auto finger : Core::kFingers) {
        if (a_sourceFingerMask.Occupies(finger)) {
            mask.Add(Core::Target {.hand = a_target.hand, .finger = finger});
        }
    }

    return mask;
}
}
