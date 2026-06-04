#include "SourceModelFootprints.h"

#include "ModelPaths.h"

#include <algorithm>
#include <array>
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
    constexpr auto kRingBodyPart = std::uint16_t {36};
    constexpr auto kMinimumSkinWeight = 0.000001F;
    constexpr auto kMeaningfulSkinWeightRatio = 0.10F;

    enum class DismemberModelScan {
        kNoDismemberPartitions,
        kNonRingPartitions,
        kRingPartitions,
    };

    constexpr auto kNonFingerWeightBucket = Core::kFingers.size();
    constexpr auto kWeightBucketCount = Core::kFingers.size() + 1;

    struct FingerBone {
        Core::Hand hand {Core::Hand::kRight};
        Core::Finger finger {Core::Finger::kIndex};
        std::uint8_t segment {0};

        [[nodiscard]] bool operator==(const FingerBone&) const = default;
    };

    struct SkinVertexWeight {
        std::uint16_t vertex {0};
        std::size_t bucket {0};
        float weight {0.0F};
    };

    struct SkinInfluenceScan {
        Core::FingerMask fingerMask;
        bool hasWeightedInfluence {false};
        bool hasMeaningfulFingerInfluence {false};
        bool hasMeaningfulNonFingerInfluence {false};
    };

    struct SourceModelFootprint {
        Core::FingerMask fingerMask;
        bool hasRingEvidence {false};
    };

    std::mutex g_cacheLock;

    [[nodiscard]] std::unordered_map<RE::FormID, SourceModelFootprint>& SourceModelFootprints() {
        static auto* footprints = new std::unordered_map<RE::FormID, SourceModelFootprint>();
        return *footprints;
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

    [[nodiscard]] std::size_t WeightBucketForBoneName(const char* a_boneName) {
        if (a_boneName && a_boneName[0] != '\0') {
            if (const auto parsedBone = ParseFingerBoneName(a_boneName)) {
                return std::to_underlying(parsedBone->finger);
            }
        }

        return kNonFingerWeightBucket;
    }

    [[nodiscard]] bool HasSkinWeightData(const RE::NiSkinInstance& a_skin) {
        return a_skin.skinData && a_skin.skinData->boneData;
    }

    void CollectSkinBoneWeights(
        const RE::NiSkinInstance& a_skin,
        const std::uint32_t a_boneIndex,
        std::vector<SkinVertexWeight>& a_weights
    ) {
        const auto* bone = a_skin.bones ? a_skin.bones[a_boneIndex] : nullptr;
        const auto* boneName = bone ? bone->name.c_str() : nullptr;
        const auto bucket = WeightBucketForBoneName(boneName);
        const auto& boneData = a_skin.skinData->boneData[a_boneIndex];
        if (!boneData.boneVertData || boneData.verts == 0) {
            return;
        }

        for (std::uint16_t weightIndex = 0; weightIndex < boneData.verts; ++weightIndex) {
            const auto& weight = boneData.boneVertData[weightIndex];
            if (weight.weight <= kMinimumSkinWeight) {
                continue;
            }

            a_weights.push_back(
                SkinVertexWeight {
                    .vertex = weight.vert,
                    .bucket = bucket,
                    .weight = weight.weight,
                }
            );
        }
    }

    [[nodiscard]] std::vector<SkinVertexWeight> CollectSkinVertexWeights(const RE::NiSkinInstance& a_skin) {
        std::vector<SkinVertexWeight> weights;
        if (!HasSkinWeightData(a_skin)) {
            return weights;
        }

        const auto boneCount = a_skin.skinData->bones;
        for (std::uint32_t boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
            CollectSkinBoneWeights(a_skin, boneIndex, weights);
        }

        std::ranges::sort(weights, [](const auto& a_lhs, const auto& a_rhs) {
            return a_lhs.vertex < a_rhs.vertex;
        });
        return weights;
    }

    void CollectFingerMaskFromBoneNames(const RE::NiSkinInstance& a_skin, Core::FingerMask& a_fingerMask) {
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

    void AddMeaningfulVertexWeights(
        SkinInfluenceScan& a_result,
        const std::vector<SkinVertexWeight>& a_weights,
        const std::size_t a_begin,
        const std::size_t a_end
    ) {
        std::array<float, kWeightBucketCount> bucketWeights {};
        auto strongestWeight = 0.0F;
        for (auto index = a_begin; index < a_end; ++index) {
            bucketWeights[a_weights[index].bucket] += a_weights[index].weight;
            strongestWeight = std::max(strongestWeight, bucketWeights[a_weights[index].bucket]);
        }

        const auto meaningfulWeight = strongestWeight * kMeaningfulSkinWeightRatio;
        for (std::size_t bucket = 0; bucket < bucketWeights.size(); ++bucket) {
            if (bucketWeights[bucket] <= kMinimumSkinWeight || bucketWeights[bucket] < meaningfulWeight) {
                continue;
            }

            if (bucket == kNonFingerWeightBucket) {
                a_result.hasMeaningfulNonFingerInfluence = true;
                continue;
            }

            a_result.fingerMask.Add(static_cast<Core::Finger>(bucket));
            a_result.hasMeaningfulFingerInfluence = true;
        }
    }

    [[nodiscard]] SkinInfluenceScan ScanSkinInfluences(const RE::NiSkinInstance& a_skin) {
        SkinInfluenceScan result;
        const auto weights = CollectSkinVertexWeights(a_skin);
        if (weights.empty()) {
            return result;
        }

        result.hasWeightedInfluence = true;
        auto rangeStart = std::size_t {0};
        while (rangeStart < weights.size()) {
            auto rangeEnd = rangeStart;
            while (rangeEnd < weights.size() && weights[rangeEnd].vertex == weights[rangeStart].vertex) {
                ++rangeEnd;
            }

            AddMeaningfulVertexWeights(result, weights, rangeStart, rangeEnd);
            rangeStart = rangeEnd;
        }
        return result;
    }

    void CollectFingerMaskFromSkin(const RE::NiSkinInstance& a_skin, Core::FingerMask& a_fingerMask) {
        const auto scan = ScanSkinInfluences(a_skin);
        if (!scan.hasWeightedInfluence) {
            CollectFingerMaskFromBoneNames(a_skin, a_fingerMask);
            return;
        }

        for (const auto finger : Core::kFingers) {
            if (scan.fingerMask.Occupies(finger)) {
                a_fingerMask.Add(finger);
            }
        }
    }

    [[nodiscard]] bool HasRingPartition(RE::BSDismemberSkinInstance& a_skin) {
        auto& runtimeData = a_skin.GetRuntimeData();
        if (!runtimeData.partitions || runtimeData.numPartitions <= 0) {
            return false;
        }

        for (auto index = 0; index < runtimeData.numPartitions; ++index) {
            if (runtimeData.partitions[index].slot == kRingBodyPart) {
                return true;
            }
        }

        return false;
    }

    [[nodiscard]] bool HasOnlyFingerBoneNames(const RE::NiSkinInstance& a_skin) {
        const auto boneCount = a_skin.skinData ? a_skin.skinData->bones : a_skin.numMatrices;
        auto sawFingerBone = false;

        for (std::uint32_t index = 0; index < boneCount; ++index) {
            const auto* bone = a_skin.bones ? a_skin.bones[index] : nullptr;
            const auto* boneName = bone ? bone->name.c_str() : nullptr;
            if (!boneName || boneName[0] == '\0') {
                return false;
            }

            if (!ParseFingerBoneName(boneName)) {
                return false;
            }

            sawFingerBone = true;
        }

        return sawFingerBone;
    }

    [[nodiscard]] DismemberModelScan ScanDismemberPartitions(RE::NiAVObject& a_root, Core::FingerMask* a_fingerMask) {
        auto sawDismemberPartitions = false;
        auto foundRingPartitions = false;

        RE::BSVisit::TraverseScenegraphGeometries(std::addressof(a_root), [&](RE::BSGeometry* a_geometry) {
            if (!a_geometry) {
                return RE::BSVisit::BSVisitControl::kContinue;
            }

            auto& geometryData = a_geometry->GetGeometryRuntimeData();
            auto* skin = geometryData.skinInstance.get();
            auto* dismemberSkin = skin ? netimmerse_cast<RE::BSDismemberSkinInstance*>(skin) : nullptr;
            if (!dismemberSkin) {
                return RE::BSVisit::BSVisitControl::kContinue;
            }

            auto& runtimeData = dismemberSkin->GetRuntimeData();
            if (!runtimeData.partitions || runtimeData.numPartitions <= 0) {
                return RE::BSVisit::BSVisitControl::kContinue;
            }

            sawDismemberPartitions = true;
            if (HasRingPartition(*dismemberSkin) || HasOnlyMeaningfulFingerWeights(*skin)) {
                foundRingPartitions = true;
                if (a_fingerMask) {
                    CollectFingerMaskFromSkin(*skin, *a_fingerMask);
                }
            }

            return RE::BSVisit::BSVisitControl::kContinue;
        });

        if (foundRingPartitions) {
            return DismemberModelScan::kRingPartitions;
        }

        return sawDismemberPartitions ? DismemberModelScan::kNonRingPartitions
                                      : DismemberModelScan::kNoDismemberPartitions;
    }

    void CollectFingerMaskFromNodeNames(RE::NiAVObject& a_root, Core::FingerMask& a_fingerMask) {
        RE::BSVisit::TraverseScenegraphObjects(std::addressof(a_root), [&](RE::NiAVObject* a_object) {
            const auto* nodeName = a_object ? a_object->name.c_str() : nullptr;
            if (nodeName && nodeName[0] != '\0') {
                if (const auto parsedBone = ParseFingerBoneName(nodeName)) {
                    a_fingerMask.Add(parsedBone->finger);
                }
            }

            return RE::BSVisit::BSVisitControl::kContinue;
        });
    }

    void CollectFingerMaskFromAllSkins(RE::NiAVObject& a_root, Core::FingerMask& a_fingerMask) {
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

    void MergeFingerMask(Core::FingerMask& a_target, const Core::FingerMask& a_source) {
        for (const auto finger : Core::kFingers) {
            if (a_source.Occupies(finger)) {
                a_target.Add(finger);
            }
        }
    }

    [[nodiscard]] SourceModelFootprint ScanModelFootprint(RE::NiAVObject& a_root) {
        SourceModelFootprint footprint;
        const auto dismemberScan = ScanDismemberPartitions(a_root, std::addressof(footprint.fingerMask));
        if (dismemberScan != DismemberModelScan::kNoDismemberPartitions) {
            footprint.hasRingEvidence = dismemberScan == DismemberModelScan::kRingPartitions;
            return footprint;
        }

        CollectFingerMaskFromNodeNames(a_root, footprint.fingerMask);
        CollectFingerMaskFromAllSkins(a_root, footprint.fingerMask);
        footprint.hasRingEvidence = !footprint.fingerMask.Empty();
        return footprint;
    }

    [[nodiscard]] std::vector<std::string> GetSourceModelPaths(const RE::TESObjectARMO& a_ring) {
        std::vector<std::string> paths;

        for (const auto* addon : a_ring.armorAddons) {
            if (!addon) {
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

    void MergeModelFootprint(const std::string& a_path, SourceModelFootprint& a_footprint) {
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

        const auto modelFootprint = ScanModelFootprint(*sourceRoot);
        MergeFingerMask(a_footprint.fingerMask, modelFootprint.fingerMask);
        a_footprint.hasRingEvidence = a_footprint.hasRingEvidence || modelFootprint.hasRingEvidence;
    }

    [[nodiscard]] SourceModelFootprint DetectSourceModelFootprint(const RE::TESObjectARMO& a_ring) {
        SourceModelFootprint footprint;
        for (const auto& path : GetSourceModelPaths(a_ring)) {
            MergeModelFootprint(path, footprint);
        }

        return footprint;
    }

    [[nodiscard]] std::uint8_t RetargetSegment(const FingerBone& a_source, const Core::Finger a_targetFinger) {
        if (a_targetFinger != Core::Finger::kThumb || a_source.finger == Core::Finger::kThumb) {
            return a_source.segment;
        }

        constexpr auto kLastThumbSegment = std::uint8_t {2};
        const auto segment = static_cast<std::uint8_t>(a_source.segment + 1);
        return std::min(segment, kLastThumbSegment);
    }

    [[nodiscard]] std::optional<Core::Finger> FirstOccupiedFinger(const Core::FingerMask& a_sourceFingerMask) {
        for (const auto finger : Core::kFingers) {
            if (a_sourceFingerMask.Occupies(finger)) {
                return finger;
            }
        }

        return std::nullopt;
    }

    [[nodiscard]] std::optional<Core::Finger> RetargetFinger(
        const Core::Finger a_source,
        const Core::Finger a_anchor,
        const Core::FingerMask& a_sourceFingerMask
    ) {
        if (!a_sourceFingerMask.IsMultiFinger()) {
            return a_anchor;
        }

        const auto firstOccupiedFinger = FirstOccupiedFinger(a_sourceFingerMask);
        if (!firstOccupiedFinger) {
            return std::nullopt;
        }

        const auto sourceIndex = static_cast<std::size_t>(std::to_underlying(a_source));
        const auto firstIndex = static_cast<std::size_t>(std::to_underlying(*firstOccupiedFinger));
        if (sourceIndex < firstIndex) {
            return std::nullopt;
        }

        const auto targetIndex = static_cast<std::size_t>(std::to_underlying(a_anchor)) + (sourceIndex - firstIndex);
        if (targetIndex >= Core::kFingers.size()) {
            return std::nullopt;
        }

        return Core::kFingers[targetIndex];
    }

    [[nodiscard]] std::optional<FingerBone> RetargetFingerBone(
        const FingerBone& a_source,
        const Core::Target a_target,
        const Core::FingerMask& a_sourceFingerMask
    ) {
        const auto targetFinger = RetargetFinger(a_source.finger, a_target.finger, a_sourceFingerMask);
        if (!targetFinger) {
            return std::nullopt;
        }

        return FingerBone {
            .hand = a_target.hand,
            .finger = *targetFinger,
            .segment = RetargetSegment(a_source, *targetFinger),
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

    const auto retargetedBone = RetargetFingerBone(*sourceBone, a_target, a_sourceFingerMask);
    return retargetedBone ? std::make_optional(MakeFingerBoneName(*retargetedBone)) : std::nullopt;
}

bool HasOnlyMeaningfulFingerWeights(const RE::NiSkinInstance& a_skin) {
    const auto scan = ScanSkinInfluences(a_skin);
    if (!scan.hasWeightedInfluence) {
        return HasOnlyFingerBoneNames(a_skin);
    }

    return scan.hasMeaningfulFingerInfluence && !scan.hasMeaningfulNonFingerInfluence;
}

bool IsRingModel(RE::NiAVObject& a_root) {
    return ScanModelFootprint(a_root).hasRingEvidence;
}

namespace {
    [[nodiscard]] SourceModelFootprint GetSourceModelFootprint(const RE::TESObjectARMO& a_ring) {
        const auto formID = a_ring.GetFormID();
        {
            std::scoped_lock lock(g_cacheLock);
            const auto& sourceModelFootprints = SourceModelFootprints();
            if (const auto cached = sourceModelFootprints.find(formID); cached != sourceModelFootprints.end()) {
                return cached->second;
            }
        }

        const auto footprint = DetectSourceModelFootprint(a_ring);
        std::scoped_lock lock(g_cacheLock);
        return SourceModelFootprints().try_emplace(formID, footprint).first->second;
    }
}

bool HasRingModelEvidence(const RE::TESObjectARMO& a_ring) {
    return GetSourceModelFootprint(a_ring).hasRingEvidence;
}

Core::FingerMask GetSourceFingerMask(const RE::TESObjectARMO& a_ring) {
    return GetSourceModelFootprint(a_ring).fingerMask;
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
            const auto targetFinger = RetargetFinger(finger, a_target.finger, a_sourceFingerMask);
            if (!targetFinger) {
                return {};
            }

            mask.Add(Core::Target {.hand = a_target.hand, .finger = *targetFinger});
        }
    }

    return mask;
}
}
