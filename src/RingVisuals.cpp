#include "RingVisuals.h"

#include "RingFootprints.h"
#include "RingTargets.h"
#include "VirtualRings.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <mutex>
#include <optional>
#include <string>

namespace RingVisuals {
namespace {
    constexpr auto* kRootNodeName = "LHRSVirtualRings";
    constexpr auto kLeftHandNode = "NPC L Hand [LHnd]"sv;
    constexpr auto kRightHandNode = "NPC R Hand [RHnd]"sv;

    std::mutex g_lock;
    std::atomic_bool g_firstPersonRetryQueued {false};

    struct ModelVariantSelection {
        const VirtualRings::SourceAddonVisual::ModelVariant* model {nullptr};
    };

    struct SkinPatchResult {
        bool foundSkin {false};
        bool retargetedSkin {false};
    };

    [[nodiscard]] RE::SEX OppositeSex(const RE::SEX a_sex) {
        return a_sex == RE::SEXES::kFemale ? RE::SEXES::kMale : RE::SEXES::kFemale;
    }

    [[nodiscard]] RE::Actor* AsActor(RE::TESObjectREFR* a_ref) {
        return a_ref ? skyrim_cast<RE::Actor*>(a_ref) : nullptr;
    }

    [[nodiscard]] RE::SEX GetActorSex(RE::TESObjectREFR* a_actor) {
        const auto* actor = AsActor(a_actor);
        const auto* actorBase = actor ? actor->GetActorBase() : nullptr;
        return actorBase ? actorBase->GetSex() : RE::SEXES::kMale;
    }

    [[nodiscard]] const VirtualRings::SourceAddonVisual::ModelVariant& GetModelVariant(
        const VirtualRings::SourceAddonVisual& a_visual,
        const RE::SEX a_sex,
        const bool a_firstPerson
    ) {
        const auto sexIndex = std::to_underlying(a_sex);
        return a_firstPerson ? a_visual.firstPerson[sexIndex] : a_visual.thirdPerson[sexIndex];
    }

    [[nodiscard]] std::optional<ModelVariantSelection> SelectCandidateModelVariant(
        const VirtualRings::SourceAddonVisual& a_visual,
        const RE::SEX a_selectedSex,
        const bool a_selectedFirstPerson
    ) {
        const auto& model = GetModelVariant(a_visual, a_selectedSex, a_selectedFirstPerson);
        if (model.path.empty()) {
            return std::nullopt;
        }

        return ModelVariantSelection {
            .model = std::addressof(model),
        };
    }

    [[nodiscard]] std::optional<ModelVariantSelection> SelectModelVariant(
        const VirtualRings::SourceAddonVisual& a_visual,
        RE::TESObjectREFR* a_actor,
        const bool a_firstPerson
    ) {
        const auto sex = GetActorSex(a_actor);
        const auto oppositeSex = OppositeSex(sex);

        if (auto selection = SelectCandidateModelVariant(a_visual, sex, a_firstPerson)) {
            return selection;
        }

        if (auto selection = SelectCandidateModelVariant(a_visual, sex, !a_firstPerson)) {
            return selection;
        }

        if (auto selection = SelectCandidateModelVariant(a_visual, oppositeSex, a_firstPerson)) {
            return selection;
        }

        if (auto selection = SelectCandidateModelVariant(a_visual, oppositeSex, !a_firstPerson)) {
            return selection;
        }

        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> GetRetargetedNodeName(
        const RingTarget a_target,
        const RingFootprints::RingFootprint& a_footprint,
        const std::string_view a_name
    ) {
        if (a_name == kLeftHandNode || a_name == kRightHandNode) {
            return a_target.hand == RingHand::kLeft ? std::string {kLeftHandNode} : std::string {kRightHandNode};
        }

        if (const auto bone = RingFootprints::ParseFingerBoneName(a_name)) {
            return RingFootprints::MakeFingerBoneName(RingFootprints::RetargetBone(*bone, a_target, a_footprint));
        }

        return std::nullopt;
    }

    [[nodiscard]] RE::NiAVObject* FindBone(RE::BipedAnim& a_biped, const char* a_name) {
        if (!a_biped.root || !a_name || a_name[0] == '\0') {
            return nullptr;
        }

        return a_biped.root->GetObjectByName(RE::BSFixedString {a_name});
    }

    [[nodiscard]] RE::NiAVObject* FindRetargetedBone(
        RE::BipedAnim& a_biped,
        const RingTarget a_target,
        const RingFootprints::RingFootprint& a_footprint,
        const RE::NiAVObject& a_bone
    ) {
        const auto* boneName = a_bone.name.c_str();
        if (!boneName || boneName[0] == '\0') {
            return nullptr;
        }

        const auto retargetedName = GetRetargetedNodeName(a_target, a_footprint, boneName);
        if (!retargetedName) {
            return nullptr;
        }

        auto* retargetedBone = FindBone(a_biped, retargetedName->c_str());
        return retargetedBone;
    }

    [[nodiscard]] bool RenameRetargetedNodes(
        RE::NiAVObject& a_root,
        const RingTarget a_target,
        const RingFootprints::RingFootprint& a_footprint
    ) {
        bool patched = false;
        RE::BSVisit::TraverseScenegraphObjects(std::addressof(a_root), [&](RE::NiAVObject* a_object) {
            if (!a_object) {
                return RE::BSVisit::BSVisitControl::kContinue;
            }

            const auto* nodeName = a_object->name.c_str();
            if (nodeName && nodeName[0] != '\0') {
                if (const auto retargetedName = GetRetargetedNodeName(a_target, a_footprint, nodeName)) {
                    a_object->name = RE::BSFixedString {retargetedName->c_str()};
                    patched = true;
                }
            }

            return RE::BSVisit::BSVisitControl::kContinue;
        });
        return patched;
    }

    void EnableDismemberPartitions(RE::BSDismemberSkinInstance& a_skin) {
        auto& runtimeData = a_skin.GetRuntimeData();
        if (!runtimeData.partitions || runtimeData.numPartitions <= 0) {
            return;
        }

        for (auto index = 0; index < runtimeData.numPartitions; ++index) {
            a_skin.UpdateDismemberPartion(runtimeData.partitions[index].slot, true);
        }
    }

    [[nodiscard]] bool PatchSkinInstance(
        RE::BipedAnim& a_biped,
        const RingTarget a_target,
        const RingFootprints::RingFootprint& a_footprint,
        RE::NiSkinInstance& a_skin
    ) {
        bool retargetedThisSkin = false;
        const auto boneCount = a_skin.skinData ? a_skin.skinData->bones : a_skin.numMatrices;
        for (std::uint32_t index = 0; index < boneCount; ++index) {
            const auto* bone = a_skin.bones ? a_skin.bones[index] : nullptr;
            auto* retargetedBone = bone ? FindRetargetedBone(a_biped, a_target, a_footprint, *bone) : nullptr;
            if (!retargetedBone) {
                continue;
            }

            a_skin.bones[index] = retargetedBone;
            if (a_skin.boneWorldTransforms) {
                a_skin.boneWorldTransforms[index] = std::addressof(retargetedBone->world);
            }

            retargetedThisSkin = true;
        }

        if (auto* dismemberSkin = netimmerse_cast<RE::BSDismemberSkinInstance*>(std::addressof(a_skin))) {
            EnableDismemberPartitions(*dismemberSkin);
        }

        if (retargetedThisSkin) {
            a_skin.frameID = std::numeric_limits<std::uint32_t>::max();
        }

        return retargetedThisSkin;
    }

    [[nodiscard]] SkinPatchResult PatchSkinBindings(
        RE::BipedAnim& a_biped,
        const RingTarget a_target,
        const RingFootprints::RingFootprint& a_footprint,
        RE::NiAVObject& a_root
    ) {
        SkinPatchResult result;
        RE::BSVisit::TraverseScenegraphGeometries(std::addressof(a_root), [&](RE::BSGeometry* a_geometry) {
            if (!a_geometry) {
                return RE::BSVisit::BSVisitControl::kContinue;
            }

            auto& geometryData = a_geometry->GetGeometryRuntimeData();
            auto* skin = geometryData.skinInstance.get();
            if (!skin) {
                return RE::BSVisit::BSVisitControl::kContinue;
            }

            result.foundSkin = true;
            result.retargetedSkin = PatchSkinInstance(a_biped, a_target, a_footprint, *skin) || result.retargetedSkin;
            return RE::BSVisit::BSVisitControl::kContinue;
        });
        return result;
    }

    [[nodiscard]] bool PatchScenegraph(
        RE::BipedAnim& a_biped,
        RE::NiAVObject& a_root,
        const RingTarget a_target,
        const RingFootprints::RingFootprint& a_footprint
    ) {
        if (!a_biped.root) {
            return false;
        }

        const auto skinPatch = PatchSkinBindings(a_biped, a_target, a_footprint, a_root);
        const auto nodeNamesPatched = RenameRetargetedNodes(a_root, a_target, a_footprint);
        return skinPatch.foundSkin ? skinPatch.retargetedSkin : nodeNamesPatched;
    }

    void ApplyTextureSwap(const VirtualRings::SourceAddonVisual::ModelVariant& a_model, RE::NiAVObject& a_root) {
        if (!a_model.textureSwap || a_model.numAlternateTextures == 0) {
            return;
        }

        using ApplyTextureSwap_t = void (*)(RE::TESModelTextureSwap*, RE::NiAVObject*) noexcept;
        static REL::Relocation<ApplyTextureSwap_t> applyTextureSwap {REL::VariantID(14660, 14837, 0x1AB7D0)};
        applyTextureSwap(const_cast<RE::TESModelTextureSwap*>(a_model.textureSwap), std::addressof(a_root));
    }

    [[nodiscard]] RE::NiPointer<RE::NiAVObject> BuildAddonVisualNode(
        RE::TESObjectREFR& a_actor,
        RE::BipedAnim& a_biped,
        const bool a_firstPerson,
        const RE::FormID a_sourceFormID,
        const RingFootprints::RingFootprint& a_footprint,
        const VirtualRings::SourceAddonVisual& a_visual
    ) {
        const auto selection = SelectModelVariant(a_visual, std::addressof(a_actor), a_firstPerson);
        if (!selection || !selection->model) {
            return nullptr;
        }

        RE::NiPointer<RE::NiNode> sourceRoot;
        const RE::BSModelDB::DBTraits::ArgsType args;
        const auto loadResult = RE::BSModelDB::Demand(selection->model->path.c_str(), sourceRoot, args);
        if (loadResult != RE::BSResource::ErrorCode::kNone || !sourceRoot) {
            logger::warn(
                "RingVisuals: source load failed | target={} | source={:08X} | addon={:08X} | path='{}' | error={}",
                TargetLabel(a_visual.target),
                a_sourceFormID,
                a_visual.sourceAddon ? a_visual.sourceAddon->GetFormID() : 0,
                selection->model->path,
                std::to_underlying(loadResult)
            );
            return nullptr;
        }

        RE::NiPointer<RE::NiObject> object;
        sourceRoot->CreateDeepCopy(object);
        auto* node = object ? object->AsNode() : nullptr;
        if (!node) {
            logger::warn(
                "RingVisuals: deep copy failed | target={} | source={:08X} | addon={:08X} | path='{}' | reason=noNiNode",
                TargetLabel(a_visual.target),
                a_sourceFormID,
                a_visual.sourceAddon ? a_visual.sourceAddon->GetFormID() : 0,
                selection->model->path
            );
            return nullptr;
        }

        ApplyTextureSwap(*selection->model, *node);
        if (!PatchScenegraph(a_biped, *node, a_visual.target, a_footprint)) {
            logger::warn(
                "RingVisuals: patch failed | target={} | source={:08X} | addon={:08X} | path='{}' | reason=noEditableRingData",
                TargetLabel(a_visual.target),
                a_sourceFormID,
                a_visual.sourceAddon ? a_visual.sourceAddon->GetFormID() : 0,
                selection->model->path
            );
            return nullptr;
        }

        node->SetAppCulled(false);
        return RE::NiPointer<RE::NiAVObject> {node};
    }

    void UpdateAttachedNode(RE::NiAVObject& a_node, RE::NiNode* a_parent) {
        RE::NiUpdateData updateData {};
        updateData.flags.set(RE::NiUpdateData::Flag::kDirty);
        a_node.UpdateDownwardPass(updateData, 0);
        if (a_parent) {
            a_parent->UpdateDownwardPass(updateData, 0);
        }
    }

    void DetachExistingRoot(RE::BipedAnim& a_biped) {
        if (!a_biped.root) {
            return;
        }

        auto* existing = a_biped.root->GetObjectByName(RE::BSFixedString {kRootNodeName});
        if (!existing || existing->parent != a_biped.root) {
            return;
        }

        RE::NiPointer<RE::NiAVObject> detached;
        a_biped.root->DetachChild(existing, detached);
    }

    [[nodiscard]] RE::NiPointer<RE::NiNode> BuildTargetNode(
        RE::TESObjectREFR& a_actor,
        RE::BipedAnim& a_biped,
        const bool a_firstPerson,
        const VirtualRings::VisualEntry& a_entry
    ) {
        RE::NiPointer<RE::NiNode> targetNode {RE::NiNode::Create()};
        if (!targetNode) {
            return nullptr;
        }

        const auto targetName = std::string {TargetLabel(a_entry.target)};
        targetNode->name = RE::BSFixedString {targetName.c_str()};
        for (const auto& visual : a_entry.addonVisuals) {
            auto addonNode = BuildAddonVisualNode(
                a_actor,
                a_biped,
                a_firstPerson,
                a_entry.sourceFormID,
                a_entry.footprint,
                visual
            );
            if (addonNode) {
                targetNode->AttachChild(addonNode.get(), true);
            }
        }

        if (targetNode->GetChildren().empty()) {
            return nullptr;
        }

        targetNode->SetAppCulled(false);
        return targetNode;
    }

    void RefreshBiped(
        RE::TESObjectREFR& a_actor,
        RE::BipedAnim& a_biped,
        const bool a_firstPerson,
        const std::vector<VirtualRings::VisualEntry>& a_entries
    ) {
        if (!a_biped.root) {
            return;
        }

        DetachExistingRoot(a_biped);
        if (a_entries.empty()) {
            return;
        }

        RE::NiPointer<RE::NiNode> root {RE::NiNode::Create()};
        if (!root) {
            return;
        }

        root->name = RE::BSFixedString {kRootNodeName};
        for (const auto& entry : a_entries) {
            auto targetNode = BuildTargetNode(a_actor, a_biped, a_firstPerson, entry);
            if (targetNode) {
                root->AttachChild(targetNode.get(), true);
            }
        }

        if (root->GetChildren().empty()) {
            return;
        }

        root->SetAppCulled(false);
        a_biped.root->AttachChild(root.get(), true);
        UpdateAttachedNode(*root, a_biped.root);
    }

    void QueueFirstPersonRetry() {
        if (g_firstPersonRetryQueued.exchange(true)) {
            return;
        }

        stl::add_thread_task(
            [] {
                g_firstPersonRetryQueued.store(false);
                RequestRefresh();
            },
            500ms
        );
    }

    void RefreshPlayer() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return;
        }

        const auto entries = VirtualRings::GetVisualEntries();
        const auto& thirdPersonBiped = player->GetBiped(false);
        if (thirdPersonBiped) {
            RefreshBiped(*player, *thirdPersonBiped, false, entries);
        }

        const auto& firstPersonBiped = player->GetBiped(true);
        if (firstPersonBiped) {
            RefreshBiped(*player, *firstPersonBiped, true, entries);
        } else if (!entries.empty()) {
            QueueFirstPersonRetry();
        }
    }
}

void RequestRefresh() {
    stl::add_task([] {
        Refresh();
    });
}

void Refresh() {
    std::scoped_lock lock(g_lock);
    RefreshPlayer();
}

void Revert() {
    std::scoped_lock lock(g_lock);
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return;
    }

    for (const auto firstPerson : {false, true}) {
        const auto& biped = player->GetBiped(firstPerson);
        if (biped) {
            DetachExistingRoot(*biped);
        }
    }
}
}
