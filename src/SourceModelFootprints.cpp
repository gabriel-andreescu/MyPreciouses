#include "SourceModelFootprints.h"

#include "Core/TargetProjection.h"
#include "ModelPaths.h"

#include <RE/N/NiSkinData.h>
#include <RE/N/NiSkinPartition.h>

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
    constexpr auto kLeftHandNode = "NPC L Hand [LHnd]"sv;
    constexpr auto kRightHandNode = "NPC R Hand [RHnd]"sv;
    constexpr auto kNodeCodeSuffix = "]"sv;
    constexpr auto kRingBodyPart = std::uint16_t {36};
    constexpr auto kMinimumSkinWeight = 0.000001F;
    constexpr auto kMeaningfulSkinWeightRatio = 0.10F;

    constexpr auto kNonTargetWeightBucket = Core::kAllTargets.size();
    constexpr auto kWeightBucketCount = Core::kAllTargets.size() + 1;

    struct FingerBone {
        Core::Hand hand {Core::Hand::kRight};
        Core::Finger finger {Core::Finger::kIndex};
        std::uint8_t segment {0};

        [[nodiscard]] bool operator==(const FingerBone&) const = default;
    };

    struct SkinVertexWeight {
        std::uint32_t vertexKey {0};
        std::size_t bucket {0};
        float weight {0.0F};
    };

    struct SkinInfluenceScan {
        Core::TargetMask sourceTargets;
        bool hasWeightedInfluence {false};
        bool hasMeaningfulFingerInfluence {false};
        bool hasMeaningfulNonFingerInfluence {false};
    };

    struct SourceModelFootprint {
        Core::TargetMask sourceTargets;
        bool isRingModel {false};
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

    [[nodiscard]] constexpr Core::Target ToTarget(const FingerBone& a_bone) {
        return Core::Target {.hand = a_bone.hand, .finger = a_bone.finger};
    }

    [[nodiscard]] std::optional<Core::Hand> ParseHandNodeName(const std::string_view a_name) {
        if (a_name == kLeftHandNode) {
            return Core::Hand::kLeft;
        }

        if (a_name == kRightHandNode) {
            return Core::Hand::kRight;
        }

        return std::nullopt;
    }

    [[nodiscard]] std::string MakeHandNodeName(const Core::Hand a_hand) {
        return a_hand == Core::Hand::kLeft ? std::string {kLeftHandNode} : std::string {kRightHandNode};
    }

    [[nodiscard]] std::size_t WeightBucketForBoneName(const char* a_boneName) {
        if (a_boneName && a_boneName[0] != '\0') {
            if (const auto parsedBone = ParseFingerBoneName(a_boneName)) {
                return Core::ToIndex(ToTarget(*parsedBone));
            }
        }

        return kNonTargetWeightBucket;
    }

    [[nodiscard]] std::uint32_t GetSkinBoneCount(const RE::NiSkinInstance& a_skin) {
        return a_skin.skinData && a_skin.skinData->bones > 0 ? a_skin.skinData->bones : a_skin.numMatrices;
    }

    [[nodiscard]] const char* GetSkinBoneName(const RE::NiSkinInstance& a_skin, const std::uint32_t a_index) {
        if (!a_skin.bones || a_index >= GetSkinBoneCount(a_skin)) {
            return nullptr;
        }

        const auto* bone = a_skin.bones[a_index];
        return bone ? bone->name.c_str() : nullptr;
    }

    [[nodiscard]] const RE::NiSkinPartition* GetSkinPartition(const RE::NiSkinInstance& a_skin) {
        if (a_skin.skinPartition) {
            return a_skin.skinPartition.get();
        }

        return a_skin.skinData ? a_skin.skinData->skinPartition.get() : nullptr;
    }

    [[nodiscard]] bool HasPartitionWeights(const RE::NiSkinPartition::Partition& a_partition) {
        if (!a_partition.weights || !a_partition.bonePalette || !a_partition.bones) {
            return false;
        }

        return a_partition.vertices > 0 && a_partition.bonesPerVertex > 0 && a_partition.numBones > 0;
    }

    [[nodiscard]] std::uint32_t BuildPartitionVertexKey(
        const RE::NiSkinPartition::Partition& a_partition,
        const std::size_t a_partitionIndex,
        const std::uint16_t a_vertexIndex,
        const std::uint32_t a_totalVertexCount
    ) {
        if (a_partition.vertexMap) {
            const auto mappedVertex = a_partition.vertexMap[a_vertexIndex];
            if (a_totalVertexCount == 0 || mappedVertex < a_totalVertexCount) {
                return mappedVertex;
            }
        }

        constexpr auto kPartitionLocalVertexFlag = std::uint32_t {0x80000000};
        return kPartitionLocalVertexFlag | (static_cast<std::uint32_t>(a_partitionIndex) << 16) | a_vertexIndex;
    }

    void CollectSkinPartitionWeights(
        const RE::NiSkinInstance& a_skin,
        const RE::NiSkinPartition& a_skinPartition,
        std::vector<SkinVertexWeight>& a_weights
    ) {
        const auto boneCount = GetSkinBoneCount(a_skin);
        if (!a_skin.bones || boneCount == 0) {
            return;
        }

        const auto partitionCount = std::min<std::size_t>(
            a_skinPartition.numPartitions,
            a_skinPartition.partitions.size()
        );
        const auto totalVertexCount = a_skinPartition.vertexCount;

        for (std::size_t partitionIndex = 0; partitionIndex < partitionCount; ++partitionIndex) {
            const auto& partition = a_skinPartition.partitions[partitionIndex];
            if (!HasPartitionWeights(partition)) {
                continue;
            }

            const auto weightsPerVertex = static_cast<std::size_t>(partition.bonesPerVertex);
            for (std::uint16_t vertexIndex = 0; vertexIndex < partition.vertices; ++vertexIndex) {
                const auto
                    vertexKey = BuildPartitionVertexKey(partition, partitionIndex, vertexIndex, totalVertexCount);

                for (std::uint16_t weightIndex = 0; weightIndex < partition.bonesPerVertex; ++weightIndex) {
                    const auto weightOffset = (static_cast<std::size_t>(vertexIndex) * weightsPerVertex) + weightIndex;
                    const auto weight = partition.weights[weightOffset];
                    if (weight <= kMinimumSkinWeight) {
                        continue;
                    }

                    const auto partitionBoneIndex = partition.bonePalette[weightOffset];
                    if (partitionBoneIndex >= partition.numBones) {
                        continue;
                    }

                    const auto skinBoneIndex = partition.bones[partitionBoneIndex];
                    if (skinBoneIndex >= boneCount) {
                        continue;
                    }

                    a_weights.push_back(
                        SkinVertexWeight {
                            .vertexKey = vertexKey,
                            .bucket = WeightBucketForBoneName(GetSkinBoneName(a_skin, skinBoneIndex)),
                            .weight = weight,
                        }
                    );
                }
            }
        }
    }

    void CollectSourceTargetsFromBoneNames(const RE::NiSkinInstance& a_skin, Core::TargetMask& a_sourceTargets) {
        const auto boneCount = GetSkinBoneCount(a_skin);
        for (std::uint32_t index = 0; index < boneCount; ++index) {
            const auto* boneName = GetSkinBoneName(a_skin, index);
            if (!boneName || boneName[0] == '\0') {
                continue;
            }

            if (const auto parsedBone = ParseFingerBoneName(boneName)) {
                a_sourceTargets.Add(ToTarget(*parsedBone));
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

            if (bucket == kNonTargetWeightBucket) {
                a_result.hasMeaningfulNonFingerInfluence = true;
                continue;
            }

            if (const auto target = Core::FromIndex(static_cast<std::uint32_t>(bucket))) {
                a_result.sourceTargets.Add(*target);
            }
            a_result.hasMeaningfulFingerInfluence = true;
        }
    }

    [[nodiscard]] SkinInfluenceScan ScanSkinInfluences(const RE::NiSkinInstance& a_skin) {
        SkinInfluenceScan result;
        const auto* skinPartition = GetSkinPartition(a_skin);
        if (!skinPartition) {
            return result;
        }

        std::vector<SkinVertexWeight> weights;
        CollectSkinPartitionWeights(a_skin, *skinPartition, weights);
        if (weights.empty()) {
            return result;
        }

        std::ranges::sort(weights, [](const auto& a_lhs, const auto& a_rhs) {
            return a_lhs.vertexKey < a_rhs.vertexKey;
        });

        result.hasWeightedInfluence = true;
        auto rangeStart = std::size_t {0};
        while (rangeStart < weights.size()) {
            auto rangeEnd = rangeStart;
            while (rangeEnd < weights.size() && weights[rangeEnd].vertexKey == weights[rangeStart].vertexKey) {
                ++rangeEnd;
            }

            AddMeaningfulVertexWeights(result, weights, rangeStart, rangeEnd);
            rangeStart = rangeEnd;
        }
        return result;
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

    [[nodiscard]] bool HasDismemberPartition(RE::BSDismemberSkinInstance& a_skin) {
        const auto& runtimeData = a_skin.GetRuntimeData();
        return runtimeData.partitions && runtimeData.numPartitions > 0;
    }

    [[nodiscard]] bool HasNonRingPartition(RE::BSDismemberSkinInstance& a_skin) {
        auto& runtimeData = a_skin.GetRuntimeData();
        if (!runtimeData.partitions || runtimeData.numPartitions <= 0) {
            return false;
        }

        for (auto index = 0; index < runtimeData.numPartitions; ++index) {
            if (runtimeData.partitions[index].slot != kRingBodyPart) {
                return true;
            }
        }

        return false;
    }

    [[nodiscard]] bool HasOnlyFingerBoneNames(const RE::NiSkinInstance& a_skin) {
        const auto boneCount = GetSkinBoneCount(a_skin);
        auto sawFingerBone = false;

        for (std::uint32_t index = 0; index < boneCount; ++index) {
            const auto* boneName = GetSkinBoneName(a_skin, index);
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

    void CollectSourceTargetsFromNodeNames(RE::NiAVObject& a_root, Core::TargetMask& a_sourceTargets) {
        RE::BSVisit::TraverseScenegraphObjects(std::addressof(a_root), [&](RE::NiAVObject* a_object) {
            const auto* nodeName = a_object ? a_object->name.c_str() : nullptr;
            if (nodeName && nodeName[0] != '\0') {
                if (const auto parsedBone = ParseFingerBoneName(nodeName)) {
                    a_sourceTargets.Add(ToTarget(*parsedBone));
                }
            }

            return RE::BSVisit::BSVisitControl::kContinue;
        });
    }

    void MergeSourceTargets(Core::TargetMask& a_target, const Core::TargetMask a_source) {
        for (const auto sourceTarget : Core::kAllTargets) {
            if (a_source.Contains(sourceTarget)) {
                a_target.Add(sourceTarget);
            }
        }
    }

    [[nodiscard]] bool ScanSkinAsStandaloneRing(
        const RE::NiSkinInstance& a_skin,
        Core::TargetMask& a_sourceTargets,
        bool& a_hasNonRingEvidence
    ) {
        const auto scan = ScanSkinInfluences(a_skin);
        if (!scan.hasWeightedInfluence) {
            if (!HasOnlyFingerBoneNames(a_skin)) {
                a_hasNonRingEvidence = GetSkinBoneCount(a_skin) > 0;
                return false;
            }

            CollectSourceTargetsFromBoneNames(a_skin, a_sourceTargets);
            return true;
        }

        if (!scan.hasMeaningfulFingerInfluence || scan.hasMeaningfulNonFingerInfluence) {
            a_hasNonRingEvidence = true;
            return false;
        }

        MergeSourceTargets(a_sourceTargets, scan.sourceTargets);
        return true;
    }

    [[nodiscard]] SourceModelFootprint ScanModelFootprint(RE::NiAVObject& a_root) {
        SourceModelFootprint footprint;
        auto foundRingGeometry = false;
        auto foundNonRingGeometry = false;

        RE::BSVisit::TraverseScenegraphGeometries(std::addressof(a_root), [&](RE::BSGeometry* a_geometry) {
            if (!a_geometry) {
                return RE::BSVisit::BSVisitControl::kContinue;
            }

            auto& geometryData = a_geometry->GetGeometryRuntimeData();
            auto* skin = geometryData.skinInstance.get();
            if (!skin) {
                return RE::BSVisit::BSVisitControl::kContinue;
            }

            Core::TargetMask skinTargets;
            auto hasNonRingEvidence = false;
            const auto skinIsRing = ScanSkinAsStandaloneRing(*skin, skinTargets, hasNonRingEvidence);

            auto* dismemberSkin = netimmerse_cast<RE::BSDismemberSkinInstance*>(skin);
            if (dismemberSkin && HasDismemberPartition(*dismemberSkin)) {
                if (HasNonRingPartition(*dismemberSkin) || hasNonRingEvidence) {
                    foundNonRingGeometry = true;
                    return RE::BSVisit::BSVisitControl::kContinue;
                }

                if (HasRingPartition(*dismemberSkin) || skinIsRing) {
                    foundRingGeometry = true;
                    MergeSourceTargets(footprint.sourceTargets, skinTargets);
                    return RE::BSVisit::BSVisitControl::kContinue;
                }
            }

            if (skinIsRing) {
                foundRingGeometry = true;
                MergeSourceTargets(footprint.sourceTargets, skinTargets);
            } else if (hasNonRingEvidence) {
                foundNonRingGeometry = true;
            }

            return RE::BSVisit::BSVisitControl::kContinue;
        });

        Core::TargetMask nodeTargets;
        CollectSourceTargetsFromNodeNames(a_root, nodeTargets);
        if (!nodeTargets.Empty()) {
            foundRingGeometry = true;
            MergeSourceTargets(footprint.sourceTargets, nodeTargets);
        }

        footprint.isRingModel = foundRingGeometry && !foundNonRingGeometry;
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

    [[nodiscard]] std::optional<SourceModelFootprint> LoadModelFootprint(const std::string& a_path) {
        RE::NiPointer<RE::NiNode> sourceRoot;
        const RE::BSModelDB::DBTraits::ArgsType args;
        const auto loadResult = RE::BSModelDB::Demand(a_path.c_str(), sourceRoot, args);
        if (loadResult != RE::BSResource::ErrorCode::kNone || !sourceRoot) {
            logger::debug(
                "SourceModelFootprints: source load failed | path='{}' | error={}",
                a_path,
                std::to_underlying(loadResult)
            );
            return std::nullopt;
        }

        return ScanModelFootprint(*sourceRoot);
    }

    [[nodiscard]] SourceModelFootprint DetectSourceModelFootprint(const RE::TESObjectARMO& a_ring) {
        SourceModelFootprint footprint;
        auto foundLoadedModel = false;

        for (const auto& path : GetSourceModelPaths(a_ring)) {
            const auto modelFootprint = LoadModelFootprint(path);
            if (!modelFootprint) {
                continue;
            }

            foundLoadedModel = true;
            if (!modelFootprint->isRingModel) {
                footprint.isRingModel = false;
                return footprint;
            }

            MergeSourceTargets(footprint.sourceTargets, modelFootprint->sourceTargets);
        }

        footprint.isRingModel = foundLoadedModel;
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

    [[nodiscard]] std::optional<Core::Target> RetargetSourceTarget(
        const Core::Target a_source,
        const Core::Target a_target,
        const Core::TargetMask a_sourceTargets
    ) {
        if (a_sourceTargets.Count() <= 1) {
            return a_target;
        }

        const auto sourceAnchor = Core::FindSourceAnchor(a_sourceTargets);
        return sourceAnchor ? Core::ProjectSourceTarget(a_source, *sourceAnchor, a_target) : std::nullopt;
    }

    [[nodiscard]] std::optional<Core::Hand> RetargetSourceHand(
        const Core::Hand a_source,
        const Core::Target a_target,
        const Core::TargetMask a_sourceTargets
    ) {
        if (a_sourceTargets.Count() <= 1) {
            return a_target.hand;
        }

        const auto sourceAnchor = Core::FindSourceAnchor(a_sourceTargets);
        if (!sourceAnchor) {
            return std::nullopt;
        }

        return a_source == sourceAnchor->hand ? a_target.hand : Core::OppositeHand(a_target.hand);
    }

    [[nodiscard]] std::optional<FingerBone> RetargetFingerBone(
        const FingerBone& a_source,
        const Core::Target a_target,
        const Core::TargetMask a_sourceTargets
    ) {
        const auto retargetedTarget = RetargetSourceTarget(ToTarget(a_source), a_target, a_sourceTargets);
        if (!retargetedTarget) {
            return std::nullopt;
        }

        return FingerBone {
            .hand = retargetedTarget->hand,
            .finger = retargetedTarget->finger,
            .segment = RetargetSegment(a_source, retargetedTarget->finger),
        };
    }
}

std::optional<std::string> RetargetSourceNodeName(
    const std::string_view a_name,
    const Core::Target a_target,
    const Core::TargetMask a_sourceTargets
) {
    if (const auto sourceHand = ParseHandNodeName(a_name)) {
        const auto retargetedHand = RetargetSourceHand(*sourceHand, a_target, a_sourceTargets);
        return retargetedHand ? std::make_optional(MakeHandNodeName(*retargetedHand)) : std::nullopt;
    }

    const auto sourceBone = ParseFingerBoneName(a_name);
    if (!sourceBone) {
        return std::nullopt;
    }

    const auto retargetedBone = RetargetFingerBone(*sourceBone, a_target, a_sourceTargets);
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
    return ScanModelFootprint(a_root).isRingModel;
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

bool IsRingModel(const RE::TESObjectARMO& a_ring) {
    return GetSourceModelFootprint(a_ring).isRingModel;
}

Core::TargetMask GetSourceTargets(const RE::TESObjectARMO& a_ring) {
    return GetSourceModelFootprint(a_ring).sourceTargets;
}

Core::TargetMask GetProjectedTargets(const RE::TESObjectARMO& a_ring, const Core::Target a_target) {
    return GetProjectedTargets(GetSourceTargets(a_ring), a_target);
}

Core::TargetMask GetProjectedTargets(const Core::TargetMask a_sourceTargets, const Core::Target a_target) {
    return Core::ProjectSourceTargets(a_sourceTargets, a_target);
}
}
