#include "RingFootprints.h"

#include "Inventory.h"
#include "RE/N/NiSkinPartition.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <mutex>
#include <ranges>
#include <string_view>
#include <utility>
#include <vector>

namespace RingFootprints {
namespace {
    constexpr auto kLeftFingerNodePrefix = "NPC L Finger"sv;
    constexpr auto kRightFingerNodePrefix = "NPC R Finger"sv;
    constexpr auto kLeftFingerCodePrefix = " [LF"sv;
    constexpr auto kRightFingerCodePrefix = " [RF"sv;
    constexpr auto kNodeCodeSuffix = "]"sv;
    constexpr auto kRingBodyPart = std::uint16_t {36};
    constexpr auto kMinimumSkinWeight = 0.000001F;
    constexpr auto kMeaningfulSkinWeightRatio = 0.10F;
    constexpr std::array kFingers {
        RingFinger::kThumb,
        RingFinger::kIndex,
        RingFinger::kMiddle,
        RingFinger::kRing,
        RingFinger::kPinky,
    };

    std::mutex g_cacheLock;

    struct CachedFootprint {
        RE::FormID formID;
        RingFootprint footprint;
    };

    std::vector<CachedFootprint> g_footprintCache;

    enum class DismemberFootprintScan {
        kNoDismemberPartitions,
        kNonRingPartitions,
        kRingPartitions,
    };

    constexpr auto kNonFingerWeightBucket = kFingers.size();
    constexpr auto kWeightBucketCount = kFingers.size() + 1;

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

    [[nodiscard]] std::optional<RingBone> ParseFingerBoneNameForHand(
        std::string_view a_name,
        const RingHand a_hand,
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
        if (!familyNumber || *familyNumber > std::to_underlying(RingFinger::kPinky)) {
            return std::nullopt;
        }

        const auto segmentChar = fingerNumber.back();
        if (!IsAsciiDigit(segmentChar)) {
            return std::nullopt;
        }

        return RingBone {
            .hand = a_hand,
            .finger = static_cast<RingFinger>(*familyNumber),
            .segment = static_cast<std::uint8_t>(segmentChar - '0'),
        };
    }

    struct SkinVertexWeight {
        std::uint32_t vertex;
        std::size_t bucket;
        float weight;
    };

    struct SkinInfluenceScan {
        RingFootprint footprint;
        bool hasWeightedInfluence {false};
        bool hasMeaningfulFingerInfluence {false};
        bool hasMeaningfulNonFingerInfluence {false};
    };

    [[nodiscard]] std::size_t WeightBucketForBoneName(const char* a_boneName) {
        if (a_boneName && a_boneName[0] != '\0') {
            if (const auto parsedBone = RingFootprints::ParseFingerBoneName(a_boneName)) {
                return std::to_underlying(parsedBone->finger);
            }
        }

        return kNonFingerWeightBucket;
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

    [[nodiscard]] std::uint32_t GetPartitionVertexKey(
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
                const auto vertex = GetPartitionVertexKey(partition, partitionIndex, vertexIndex, totalVertexCount);

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
                            .vertex = vertex,
                            .bucket = WeightBucketForBoneName(GetSkinBoneName(a_skin, skinBoneIndex)),
                            .weight = weight,
                        }
                    );
                }
            }
        }
    }

    void CollectFootprintFromBoneNames(const RE::NiSkinInstance& a_skin, RingFootprint& a_footprint) {
        const auto boneCount = GetSkinBoneCount(a_skin);
        for (std::uint32_t index = 0; index < boneCount; ++index) {
            const auto* boneName = GetSkinBoneName(a_skin, index);
            if (!boneName || boneName[0] == '\0') {
                continue;
            }

            if (const auto parsedBone = RingFootprints::ParseFingerBoneName(boneName)) {
                a_footprint.Add(parsedBone->finger);
            }
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
            return a_lhs.vertex < a_rhs.vertex;
        });

        result.hasWeightedInfluence = true;
        auto rangeStart = weights.begin();
        while (rangeStart != weights.end()) {
            auto rangeEnd = rangeStart;
            std::array<float, kWeightBucketCount> bucketWeights {};
            auto strongestWeight = 0.0F;
            while (rangeEnd != weights.end() && rangeEnd->vertex == rangeStart->vertex) {
                bucketWeights[rangeEnd->bucket] += rangeEnd->weight;
                strongestWeight = std::max(strongestWeight, bucketWeights[rangeEnd->bucket]);
                ++rangeEnd;
            }

            const auto meaningfulWeight = strongestWeight * kMeaningfulSkinWeightRatio;
            for (std::size_t bucket = 0; bucket < bucketWeights.size(); ++bucket) {
                if (bucketWeights[bucket] <= kMinimumSkinWeight || bucketWeights[bucket] < meaningfulWeight) {
                    continue;
                }

                if (bucket == kNonFingerWeightBucket) {
                    result.hasMeaningfulNonFingerInfluence = true;
                    continue;
                }

                result.footprint.Add(static_cast<RingFinger>(bucket));
                result.hasMeaningfulFingerInfluence = true;
            }

            rangeStart = rangeEnd;
        }

        return result;
    }

    void CollectFootprintFromSkin(const RE::NiSkinInstance& a_skin, RingFootprint& a_footprint) {
        const auto scan = ScanSkinInfluences(a_skin);
        if (!scan.hasWeightedInfluence) {
            CollectFootprintFromBoneNames(a_skin, a_footprint);
            return;
        }

        a_footprint.Merge(scan.footprint);
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

            if (!RingFootprints::ParseFingerBoneName(boneName)) {
                return false;
            }

            sawFingerBone = true;
        }

        return sawFingerBone;
    }

    [[nodiscard]] DismemberFootprintScan CollectFootprintFromDismemberPartitions(
        RE::NiAVObject& a_root,
        RingFootprint& a_footprint
    ) {
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
            if (HasRingPartition(*dismemberSkin) || RingFootprints::HasOnlyMeaningfulFingerWeights(*skin)) {
                foundRingPartitions = true;
                CollectFootprintFromSkin(*skin, a_footprint);
            }

            return RE::BSVisit::BSVisitControl::kContinue;
        });

        if (foundRingPartitions) {
            return DismemberFootprintScan::kRingPartitions;
        }

        return sawDismemberPartitions ? DismemberFootprintScan::kNonRingPartitions
                                      : DismemberFootprintScan::kNoDismemberPartitions;
    }

    void CollectFootprintFromNodeNames(RE::NiAVObject& a_root, RingFootprint& a_footprint) {
        RE::BSVisit::TraverseScenegraphObjects(std::addressof(a_root), [&](RE::NiAVObject* a_object) {
            const auto* nodeName = a_object ? a_object->name.c_str() : nullptr;
            if (nodeName && nodeName[0] != '\0') {
                if (const auto parsedBone = RingFootprints::ParseFingerBoneName(nodeName)) {
                    a_footprint.Add(parsedBone->finger);
                }
            }

            return RE::BSVisit::BSVisitControl::kContinue;
        });
    }

    void CollectFootprintFromAllSkins(RE::NiAVObject& a_root, RingFootprint& a_footprint) {
        RE::BSVisit::TraverseScenegraphGeometries(std::addressof(a_root), [&](RE::BSGeometry* a_geometry) {
            if (!a_geometry) {
                return RE::BSVisit::BSVisitControl::kContinue;
            }

            auto& geometryData = a_geometry->GetGeometryRuntimeData();
            if (auto* skin = geometryData.skinInstance.get()) {
                CollectFootprintFromSkin(*skin, a_footprint);
            }

            return RE::BSVisit::BSVisitControl::kContinue;
        });
    }

    void CollectFootprintFromModel(RE::NiAVObject& a_root, RingFootprint& a_footprint) {
        const auto dismemberScan = CollectFootprintFromDismemberPartitions(a_root, a_footprint);
        if (dismemberScan == DismemberFootprintScan::kRingPartitions) {
            return;
        }

        if (dismemberScan == DismemberFootprintScan::kNonRingPartitions) {
            return;
        }

        CollectFootprintFromNodeNames(a_root, a_footprint);
        CollectFootprintFromAllSkins(a_root, a_footprint);
    }

    [[nodiscard]] const char* GetModel(const RE::TESModel& a_model) {
        const auto* model = a_model.GetModel();
        return model && model[0] != '\0' ? model : nullptr;
    }

    void AddModelPath(std::vector<std::string>& a_paths, const RE::TESModel& a_model) {
        const auto* path = GetModel(a_model);
        if (!path) {
            return;
        }

        if (std::ranges::none_of(a_paths, [path](const auto& a_existing) {
                return _stricmp(a_existing.c_str(), path) == 0;
            })) {
            a_paths.emplace_back(path);
        }
    }

    [[nodiscard]] std::vector<std::string> GetRingModelPaths(const RE::TESObjectARMO& a_ring) {
        std::vector<std::string> paths;
        const auto customRingSlots = !a_ring.HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kRing)
                                     || Inventory::HasClothingRingKeyword(std::addressof(a_ring));

        for (const auto* addon : a_ring.armorAddons) {
            if (!addon) {
                continue;
            }

            if (!customRingSlots && !addon->HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kRing)) {
                continue;
            }

            for (const auto sex : {RE::SEXES::kMale, RE::SEXES::kFemale}) {
                const auto sexIndex = std::to_underlying(sex);
                AddModelPath(paths, addon->bipedModels[sexIndex]);
                AddModelPath(paths, addon->bipedModel1stPersons[sexIndex]);
            }
        }

        return paths;
    }

    void MergeModelFootprint(const std::string& a_path, RingFootprint& a_footprint) {
        RE::NiPointer<RE::NiNode> sourceRoot;
        const RE::BSModelDB::DBTraits::ArgsType args;
        const auto loadResult = RE::BSModelDB::Demand(a_path.c_str(), sourceRoot, args);
        if (loadResult != RE::BSResource::ErrorCode::kNone || !sourceRoot) {
            logger::debug(
                "RingFootprints: source load failed | path='{}' | error={}",
                a_path,
                std::to_underlying(loadResult)
            );
            return;
        }

        CollectFootprintFromModel(*sourceRoot, a_footprint);
    }

    [[nodiscard]] RingFootprint DetectSourceRingFootprint(const RE::TESObjectARMO& a_ring) {
        RingFootprint footprint;
        for (const auto& path : GetRingModelPaths(a_ring)) {
            MergeModelFootprint(path, footprint);
        }

        return footprint;
    }

    [[nodiscard]] std::uint8_t RetargetSegment(const RingBone& a_source, const RingFinger a_targetFinger) {
        if (a_targetFinger != RingFinger::kThumb || a_source.finger == RingFinger::kThumb) {
            return a_source.segment;
        }

        constexpr auto kLastThumbSegment = std::uint8_t {2};
        const auto segment = static_cast<std::uint8_t>(a_source.segment + 1);
        return std::min(segment, kLastThumbSegment);
    }

    [[nodiscard]] std::optional<RingFinger> FirstOccupiedFinger(const RingFootprint& a_footprint) {
        for (const auto finger : kFingers) {
            if (a_footprint.Occupies(finger)) {
                return finger;
            }
        }

        return std::nullopt;
    }

    [[nodiscard]] std::optional<RingFinger> RetargetFinger(
        const RingFinger a_source,
        const RingFinger a_anchor,
        const RingFootprint& a_footprint
    ) {
        if (!a_footprint.IsMultiFinger()) {
            return a_anchor;
        }

        const auto firstOccupiedFinger = FirstOccupiedFinger(a_footprint);
        if (!firstOccupiedFinger) {
            return std::nullopt;
        }

        const auto sourceIndex = static_cast<std::size_t>(std::to_underlying(a_source));
        const auto firstIndex = static_cast<std::size_t>(std::to_underlying(*firstOccupiedFinger));
        if (sourceIndex < firstIndex) {
            return std::nullopt;
        }

        const auto targetIndex = static_cast<std::size_t>(std::to_underlying(a_anchor)) + (sourceIndex - firstIndex);
        if (targetIndex >= kFingers.size()) {
            return std::nullopt;
        }

        return kFingers[targetIndex];
    }
}

void RingFootprint::Add(const RingFinger a_finger) {
    _known = true;
    _mask |= static_cast<std::uint8_t>(1u << std::to_underlying(a_finger));
}

void RingFootprint::Merge(const RingFootprint& a_other) {
    if (!a_other.IsKnown()) {
        return;
    }

    _known = true;
    _mask |= a_other.Mask();
}

bool RingFootprint::IsKnown() const {
    return _known;
}

bool RingFootprint::IsMultiFinger() const {
    return _known && std::popcount(_mask) > 1;
}

bool RingFootprint::Occupies(const RingFinger a_finger) const {
    return (_mask & static_cast<std::uint8_t>(1u << std::to_underlying(a_finger))) != 0;
}

std::uint8_t RingFootprint::Mask() const {
    return _mask;
}

void RingTargetMask::Add(const RingTarget a_target) {
    _bits |= static_cast<std::uint16_t>(1u << ToIndex(a_target));
}

bool RingTargetMask::Empty() const {
    return _bits == 0;
}

bool RingTargetMask::Contains(const RingTarget a_target) const {
    return (_bits & static_cast<std::uint16_t>(1u << ToIndex(a_target))) != 0;
}

bool RingTargetMask::Intersects(const RingTargetMask& a_other) const {
    return (_bits & a_other._bits) != 0;
}

std::uint16_t RingTargetMask::Bits() const {
    return _bits;
}

std::optional<RingBone> ParseFingerBoneName(const std::string_view a_name) {
    if (const auto
            bone = ParseFingerBoneNameForHand(a_name, RingHand::kLeft, kLeftFingerNodePrefix, kLeftFingerCodePrefix)) {
        return bone;
    }

    return ParseFingerBoneNameForHand(a_name, RingHand::kRight, kRightFingerNodePrefix, kRightFingerCodePrefix);
}

std::string MakeFingerBoneName(const RingBone& a_bone) {
    return ::MakeFingerBoneName(RingTarget {.hand = a_bone.hand, .finger = a_bone.finger}, a_bone.segment);
}

std::optional<RingBone> RetargetBone(
    const RingBone& a_source,
    const RingTarget a_target,
    const RingFootprint& a_footprint
) {
    const auto targetFinger = RetargetFinger(a_source.finger, a_target.finger, a_footprint);
    if (!targetFinger) {
        return std::nullopt;
    }

    return RingBone {
        .hand = a_target.hand,
        .finger = *targetFinger,
        .segment = RetargetSegment(a_source, *targetFinger),
    };
}

bool HasOnlyMeaningfulFingerWeights(const RE::NiSkinInstance& a_skin) {
    const auto scan = ScanSkinInfluences(a_skin);
    if (!scan.hasWeightedInfluence) {
        return HasOnlyFingerBoneNames(a_skin);
    }

    return scan.hasMeaningfulFingerInfluence && !scan.hasMeaningfulNonFingerInfluence;
}

RingFootprint GetSourceRingFootprint(const RE::TESObjectARMO& a_ring) {
    const auto formID = a_ring.GetFormID();
    {
        std::scoped_lock lock(g_cacheLock);
        const auto cached = std::ranges::find_if(g_footprintCache, [formID](const auto& a_entry) {
            return a_entry.formID == formID;
        });
        if (cached != g_footprintCache.end()) {
            return cached->footprint;
        }
    }

    auto footprint = DetectSourceRingFootprint(a_ring);
    std::scoped_lock lock(g_cacheLock);
    g_footprintCache.emplace_back(CachedFootprint {.formID = formID, .footprint = footprint});
    return footprint;
}

RingTargetMask GetOccupiedTargets(const RE::TESObjectARMO& a_ring, const RingTarget a_target) {
    return GetOccupiedTargets(GetSourceRingFootprint(a_ring), a_target);
}

RingTargetMask GetOccupiedTargets(const RingFootprint& a_footprint, const RingTarget a_target) {
    RingTargetMask mask;
    if (!a_footprint.IsMultiFinger()) {
        mask.Add(a_target);
        return mask;
    }

    for (const auto finger : kFingers) {
        if (a_footprint.Occupies(finger)) {
            const auto targetFinger = RetargetFinger(finger, a_target.finger, a_footprint);
            if (!targetFinger) {
                return {};
            }

            mask.Add(RingTarget {.hand = a_target.hand, .finger = *targetFinger});
        }
    }

    return mask;
}
}
