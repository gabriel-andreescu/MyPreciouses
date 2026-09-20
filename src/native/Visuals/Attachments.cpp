#include "Visuals/Attachments.h"

#include <RE/Skyrim.h> // IWYU pragma: keep
#include <REL/Relocation.h>
#include <SKSE/SKSE.h> // IWYU pragma: keep

#include "Core/ActorKey.h"
#include "Core/Target.h"
#include "Core/TargetMask.h"
#include "Inventory.h"
#include "ModelPaths.h"
#include "SourceModelFootprints.h"
#include "Visuals/FirstPersonRetry.h"

#include <RE/B/BipedAnim.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Visuals::Attachments {
namespace {
    constexpr std::array<RE::SEX, RE::SEXES::kTotal> kSexes {RE::SEXES::kMale, RE::SEXES::kFemale};

    std::mutex g_lock;
    std::uint64_t g_nextRefreshGeneration {0};

    [[nodiscard]] std::unordered_set<Core::ActorKey>& TrackedActors() {
        static auto* actors = new std::unordered_set<Core::ActorKey>();
        return *actors;
    }

    [[nodiscard]] std::unordered_map<Core::ActorKey, std::uint64_t>& RefreshGenerations() {
        static auto* generations = new std::unordered_map<Core::ActorKey, std::uint64_t>();
        return *generations;
    }

    [[nodiscard]] bool IsCurrentRefreshGenerationLocked(
        const Core::ActorKey a_actor,
        const std::uint64_t a_generation
    ) {
        const auto& generations = RefreshGenerations();
        const auto generation = generations.find(a_actor);
        return generation != generations.end() && generation->second == a_generation;
    }

    [[nodiscard]] bool IsCurrentRefreshGeneration(const Core::ActorKey a_actor, const std::uint64_t a_generation) {
        std::scoped_lock const lock(g_lock);
        return IsCurrentRefreshGenerationLocked(a_actor, a_generation);
    }

    struct ModelVariant {
        std::string path;
        const RE::TESModelTextureSwap* textureSwap {nullptr};
        std::uint32_t numAlternateTextures {0};
    };

    struct ArmorAddonVisual {
        Core::Target target {Core::kDefaultLeftTarget};
        const RE::TESObjectARMA* sourceAddon {nullptr};
        std::array<ModelVariant, RE::SEXES::kTotal> thirdPerson;
        std::array<ModelVariant, RE::SEXES::kTotal> firstPerson;
    };

    struct AttachmentSourceVisuals {
        Core::Target target {Core::kDefaultLeftTarget};
        Core::TargetMask sourceTargets;
        RE::FormID sourceFormID {0};
        std::vector<ArmorAddonVisual> addonVisuals;
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

    [[nodiscard]] ModelVariant CaptureModelVariant(const RE::TESModelTextureSwap& a_model) {
        ModelVariant model;
        if (const auto* path = ModelPaths::Get(a_model)) {
            model.path = path;
            model.textureSwap = std::addressof(a_model);
            model.numAlternateTextures = a_model.numAlternateTextures;
        }

        return model;
    }

    [[nodiscard]] std::optional<ArmorAddonVisual> CaptureArmorAddonVisual(
        const RE::TESObjectARMA& a_sourceAddon,
        const Core::Target a_target,
        RE::TESRace& a_actorRace
    ) {
        if (!a_sourceAddon.IsValidRace(std::addressof(a_actorRace))) {
            return std::nullopt;
        }

        ArmorAddonVisual visual;
        visual.target = a_target;
        visual.sourceAddon = std::addressof(a_sourceAddon);
        bool foundAnyModel = false;

        for (const auto sex : kSexes) {
            const auto sexIndex = std::to_underlying(sex);
            visual.thirdPerson[sexIndex] = CaptureModelVariant(a_sourceAddon.bipedModels[sexIndex]);
            visual.firstPerson[sexIndex] = CaptureModelVariant(a_sourceAddon.bipedModel1stPersons[sexIndex]);

            foundAnyModel = foundAnyModel
                            || !visual.thirdPerson[sexIndex].path.empty()
                            || !visual.firstPerson[sexIndex].path.empty();
        }

        return foundAnyModel ? std::make_optional(std::move(visual)) : std::nullopt;
    }

    [[nodiscard]] std::vector<ArmorAddonVisual> CaptureArmorAddonVisuals(
        const RE::TESObjectARMO& a_source,
        const Core::Target a_target,
        RE::TESRace& a_actorRace
    ) {
        std::vector<ArmorAddonVisual> visuals;
        for (auto const* addon : a_source.armorAddons) {
            if (!addon) {
                continue;
            }

            if (auto visual = CaptureArmorAddonVisual(*addon, a_target, a_actorRace)) {
                visuals.push_back(std::move(*visual));
            }
        }
        return visuals;
    }

    [[nodiscard]] std::vector<AttachmentSourceVisuals> BuildAttachmentVisuals(
        const std::vector<AttachmentSource>& a_sources,
        RE::TESRace& a_actorRace
    ) {
        std::vector<AttachmentSourceVisuals> visuals;
        for (const auto& source : a_sources) {
            if (source.sourceFormID == 0) {
                continue;
            }

            auto const* sourceArmor = RE::TESForm::LookupByID<RE::TESObjectARMO>(source.sourceFormID);
            if (!sourceArmor) {
                continue;
            }

            auto addonVisuals = CaptureArmorAddonVisuals(*sourceArmor, source.target, a_actorRace);
            if (addonVisuals.empty()) {
                continue;
            }

            visuals.push_back(
                AttachmentSourceVisuals {
                    .target = source.target,
                    .sourceTargets = source.sourceTargets,
                    .sourceFormID = source.sourceFormID,
                    .addonVisuals = std::move(addonVisuals),
                }
            );
        }

        return visuals;
    }

    [[nodiscard]] const ModelVariant& GetModelVariant(
        const ArmorAddonVisual& a_visual,
        const RE::SEX a_sex,
        const bool a_firstPerson
    ) {
        const auto sexIndex = std::to_underlying(a_sex);
        return a_firstPerson ? a_visual.firstPerson[sexIndex] : a_visual.thirdPerson[sexIndex];
    }

    [[nodiscard]] const ModelVariant* SelectCandidateModelVariant(
        const ArmorAddonVisual& a_visual,
        const RE::SEX a_selectedSex,
        const bool a_selectedFirstPerson
    ) {
        const auto& model = GetModelVariant(a_visual, a_selectedSex, a_selectedFirstPerson);
        if (model.path.empty()) {
            return nullptr;
        }

        return std::addressof(model);
    }

    [[nodiscard]] const ModelVariant* SelectModelVariant(
        const ArmorAddonVisual& a_visual,
        RE::TESObjectREFR* a_actor,
        const bool a_firstPerson
    ) {
        const auto sex = GetActorSex(a_actor);
        const auto oppositeSex = OppositeSex(sex);

        if (const auto* model = SelectCandidateModelVariant(a_visual, sex, a_firstPerson)) {
            return model;
        }

        if (const auto* model = SelectCandidateModelVariant(a_visual, sex, !a_firstPerson)) {
            return model;
        }

        if (const auto* model = SelectCandidateModelVariant(a_visual, oppositeSex, a_firstPerson)) {
            return model;
        }

        if (const auto* model = SelectCandidateModelVariant(a_visual, oppositeSex, !a_firstPerson)) {
            return model;
        }

        return nullptr;
    }

    [[nodiscard]] RE::NiAVObject* FindBone(RE::BipedAnim const& a_biped, const char* a_name) {
        if (!a_biped.root || !a_name || *a_name == '\0') {
            return nullptr;
        }

        return a_biped.root->GetObjectByName(RE::BSFixedString {a_name});
    }

    [[nodiscard]] RE::NiAVObject* FindRetargetedBone(
        RE::BipedAnim const& a_biped,
        const Core::Target a_target,
        const Core::TargetMask& a_sourceTargets,
        const RE::NiAVObject& a_bone
    ) {
        if (a_bone.name.empty()) {
            return nullptr;
        }

        const auto* boneName = a_bone.name.c_str();
        const auto retargetedName = SourceModelFootprints::RetargetSourceNodeName(boneName, a_target, a_sourceTargets);
        if (!retargetedName) {
            return nullptr;
        }

        auto* retargetedBone = FindBone(a_biped, retargetedName->c_str());
        return retargetedBone;
    }

    [[nodiscard]] bool RenameRetargetedNodes(
        RE::NiAVObject& a_root,
        const Core::Target a_target,
        const Core::TargetMask& a_sourceTargets
    ) {
        bool patched = false;
        RE::BSVisit::TraverseScenegraphObjects(std::addressof(a_root), [&](RE::NiAVObject* a_object) {
            if (!a_object) {
                return RE::BSVisit::BSVisitControl::kContinue;
            }

            if (!a_object->name.empty()) {
                const auto* nodeName = a_object->name.c_str();
                if (const auto retargetedName = SourceModelFootprints::RetargetSourceNodeName(
                        nodeName,
                        a_target,
                        a_sourceTargets
                    )) {
                    a_object->name = RE::BSFixedString {retargetedName->c_str()};
                    patched = true;
                }
            }

            return RE::BSVisit::BSVisitControl::kContinue;
        });
        return patched;
    }

    void EnableDismemberPartitions(RE::BSDismemberSkinInstance& a_skin) {
        auto const& runtimeData = a_skin.GetRuntimeData();
        if (!runtimeData.partitions || runtimeData.numPartitions <= 0) {
            return;
        }

        const std::span<const RE::BSDismemberSkinInstance::Data> partitions {
            runtimeData.partitions,
            static_cast<std::size_t>(runtimeData.numPartitions),
        };
        for (const auto& partition : partitions) {
            a_skin.UpdateDismemberPartion(partition.slot, true);
        }
    }

    [[nodiscard]] bool PatchSkinInstance(
        RE::BipedAnim const& a_biped,
        const Core::Target a_target,
        const Core::TargetMask& a_sourceTargets,
        RE::NiSkinInstance& a_skin
    ) {
        bool retargetedThisSkin = false;
        const auto boneCount = a_skin.skinData ? a_skin.skinData->GetBoneCount() : a_skin.numMatrices;
        std::span<RE::NiAVObject*> bones;
        if (a_skin.bones) {
            bones = {a_skin.bones, boneCount};
        }
        std::span<const RE::NiTransform*> boneWorldTransforms;
        if (a_skin.boneWorldTransforms) {
            boneWorldTransforms = {a_skin.boneWorldTransforms, boneCount};
        }
        for (std::size_t index = 0; index < bones.size(); ++index) {
            const auto* bone = bones[index];
            auto* retargetedBone = bone ? FindRetargetedBone(a_biped, a_target, a_sourceTargets, *bone) : nullptr;
            if (!retargetedBone) {
                continue;
            }

            bones[index] = retargetedBone;
            if (!boneWorldTransforms.empty()) {
                boneWorldTransforms[index] = std::addressof(retargetedBone->world);
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
        const Core::Target a_target,
        const Core::TargetMask& a_sourceTargets,
        RE::NiAVObject& a_root
    ) {
        SkinPatchResult result;
        RE::BSVisit::TraverseScenegraphGeometries(std::addressof(a_root), [&](RE::BSGeometry* a_geometry) {
            if (!a_geometry) {
                return RE::BSVisit::BSVisitControl::kContinue;
            }

            auto const& geometryData = a_geometry->GetGeometryRuntimeData();
            auto* skin = geometryData.skinInstance.get();
            if (!skin) {
                return RE::BSVisit::BSVisitControl::kContinue;
            }

            result.foundSkin = true;
            result.retargetedSkin = PatchSkinInstance(a_biped, a_target, a_sourceTargets, *skin)
                                    || result.retargetedSkin;
            return RE::BSVisit::BSVisitControl::kContinue;
        });
        return result;
    }

    [[nodiscard]] bool PatchScenegraph(
        RE::BipedAnim& a_biped,
        RE::NiAVObject& a_root,
        const Core::Target a_target,
        const Core::TargetMask& a_sourceTargets
    ) {
        if (!a_biped.root) {
            return false;
        }

        const auto skinPatch = PatchSkinBindings(a_biped, a_target, a_sourceTargets, a_root);
        const auto nodeNamesPatched = RenameRetargetedNodes(a_root, a_target, a_sourceTargets);
        return skinPatch.foundSkin ? skinPatch.retargetedSkin : nodeNamesPatched;
    }

    [[nodiscard]] bool HasAddonOutsideRingSlot(const RE::TESObjectARMO& a_ring) {
        return std::ranges::any_of(a_ring.armorAddons, [](const auto* a_addon) {
            return a_addon && !a_addon->HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kRing);
        });
    }

    [[nodiscard]] bool NeedsVanillaRingCloneRetarget(
        const RE::TESObjectARMO& a_ring,
        const Core::TargetMask& a_sourceTargets
    ) {
        if (a_sourceTargets.Empty()) {
            return false;
        }

        const auto vanillaTargets = SourceModelFootprints::GetProjectedTargets(
            a_sourceTargets,
            Core::kVanillaRingSlotTarget
        );
        if (vanillaTargets.Empty()) {
            return false;
        }

        if (Inventory::HasClothingRingKeyword(std::addressof(a_ring)) && HasAddonOutsideRingSlot(a_ring)) {
            return true;
        }

        return vanillaTargets != a_sourceTargets;
    }

    [[nodiscard]] RE::TESObjectARMO* GetPlayerRingForBipedObjectSlot(
        RE::BipedAnim& a_biped,
        const std::int32_t a_bipedObjectSlot
    ) {
        if (a_bipedObjectSlot < 0) {
            return nullptr;
        }

        const auto bipedObjectSlot = static_cast<std::uint32_t>(a_bipedObjectSlot);
        if (bipedObjectSlot >= std::to_underlying(RE::BIPED_OBJECTS::kEditorTotal)) {
            return nullptr;
        }

        const auto actorRef = a_biped.actorRef.get();
        auto const* actor = AsActor(actorRef.get());
        if (!actor || !actor->IsPlayerRef()) {
            return nullptr;
        }

        return Inventory::AsRing(a_biped.objects[bipedObjectSlot].item);
    }

    void ApplyTextureSwap(const ModelVariant& a_model, RE::NiAVObject& a_root) {
        if (!a_model.textureSwap || a_model.numAlternateTextures == 0) {
            return;
        }

        using ApplyTextureSwapT = void (*)(RE::TESModelTextureSwap*, RE::NiAVObject*) noexcept;
        static REL::Relocation<ApplyTextureSwapT> const applyTextureSwap {REL::VariantID(14660, 14837, 0x1AB7D0)};
        applyTextureSwap(const_cast<RE::TESModelTextureSwap*>(a_model.textureSwap), std::addressof(a_root));
    }

    [[nodiscard]] RE::NiPointer<RE::NiAVObject> BuildAddonVisualNode(
        RE::TESObjectREFR& a_actor,
        RE::BipedAnim& a_biped,
        const bool a_firstPerson,
        const RE::FormID a_sourceFormID,
        const Core::TargetMask& a_sourceTargets,
        const ArmorAddonVisual& a_visual
    ) {
        const auto* model = SelectModelVariant(a_visual, std::addressof(a_actor), a_firstPerson);
        if (!model) {
            return nullptr;
        }

        RE::NiPointer<RE::NiNode> sourceRoot;
        const RE::BSModelDB::DBTraits::ArgsType args;
        const auto loadResult = RE::BSModelDB::Demand(model->path.c_str(), sourceRoot, args);
        if (loadResult != RE::BSResource::ErrorCode::kNone || !sourceRoot) {
            SKSE::log::warn(
                "Visuals: source load failed | target={} | source={:08X} | addon={:08X} | path='{}' | error={}",
                Core::TargetName(a_visual.target),
                a_sourceFormID,
                a_visual.sourceAddon ? a_visual.sourceAddon->GetFormID() : 0,
                model->path,
                std::to_underlying(loadResult)
            );
            return nullptr;
        }

        RE::NiPointer<RE::NiObject> object;
        sourceRoot->CreateDeepCopy(object);
        auto* node = object ? object->AsNode() : nullptr;
        if (!node) {
            SKSE::log::warn(
                "Visuals: deep copy failed | target={} | source={:08X} | addon={:08X} | path='{}' | reason=noNiNode",
                Core::TargetName(a_visual.target),
                a_sourceFormID,
                a_visual.sourceAddon ? a_visual.sourceAddon->GetFormID() : 0,
                model->path
            );
            return nullptr;
        }

        ApplyTextureSwap(*model, *node);
        if (!SourceModelFootprints::HasOnlyRingGeometry(*node)) {
            SKSE::log::debug(
                "Visuals: source skipped | target={} | source={:08X} | addon={:08X} | path='{}' | reason=notRingModel",
                Core::TargetName(a_visual.target),
                a_sourceFormID,
                a_visual.sourceAddon ? a_visual.sourceAddon->GetFormID() : 0,
                model->path
            );
            return nullptr;
        }

        if (!PatchScenegraph(a_biped, *node, a_visual.target, a_sourceTargets)) {
            SKSE::log::warn(
                "Visuals: patch failed | target={} | source={:08X} | addon={:08X} | path='{}' | reason=noEditableRingData",
                Core::TargetName(a_visual.target),
                a_sourceFormID,
                a_visual.sourceAddon ? a_visual.sourceAddon->GetFormID() : 0,
                model->path
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

    void DetachExistingRoot(RE::BipedAnim const& a_biped) {
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
        const AttachmentSourceVisuals& a_visuals
    ) {
        RE::NiPointer<RE::NiNode> targetNode {RE::NiNode::Create()};
        if (!targetNode) {
            return nullptr;
        }

        const auto targetName = std::string {Core::TargetName(a_visuals.target)};
        targetNode->name = RE::BSFixedString {targetName.c_str()};
        for (const auto& visual : a_visuals.addonVisuals) {
            auto const addonNode = BuildAddonVisualNode(
                a_actor,
                a_biped,
                a_firstPerson,
                a_visuals.sourceFormID,
                a_visuals.sourceTargets,
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

    void RebuildBipedAttachments(
        RE::TESObjectREFR& a_actor,
        RE::BipedAnim& a_biped,
        const bool a_firstPerson,
        const std::vector<AttachmentSourceVisuals>& a_visuals
    ) {
        if (!a_biped.root) {
            return;
        }

        DetachExistingRoot(a_biped);
        if (a_visuals.empty()) {
            return;
        }

        RE::NiPointer<RE::NiNode> const root {RE::NiNode::Create()};
        if (!root) {
            return;
        }

        root->name = RE::BSFixedString {kRootNodeName};
        for (const auto& visual : a_visuals) {
            auto const targetNode = BuildTargetNode(a_actor, a_biped, a_firstPerson, visual);
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

    void QueueFirstPersonRetry(
        const Core::ActorKey a_actor,
        std::vector<AttachmentSource> a_sources,
        const std::uint64_t a_generation
    ) {
        FirstPersonRetry::Schedule([a_actor, sources = std::move(a_sources), a_generation] {
            if (IsCurrentRefreshGeneration(a_actor, a_generation)) {
                RequestRefresh(a_actor, sources);
            }
        });
    }

    void RebuildActorAttachments(
        const Core::ActorKey a_actor,
        const std::vector<AttachmentSource>& a_sources,
        const std::uint64_t a_generation
    ) {
        auto* actor = Core::ResolveActor(a_actor);
        if (!actor) {
            return;
        }

        TrackedActors().insert(a_actor);

        auto* actorRace = actor->GetRace();
        if (!actorRace) {
            return;
        }

        const auto visuals = BuildAttachmentVisuals(a_sources, *actorRace);
        const auto& thirdPersonBiped = actor->GetBiped(false);
        if (thirdPersonBiped) {
            RebuildBipedAttachments(*actor, *thirdPersonBiped, false, visuals);
        }

        if (actor->IsPlayerRef()) {
            const auto& firstPersonBiped = actor->GetBiped(true);
            if (firstPersonBiped) {
                RebuildBipedAttachments(*actor, *firstPersonBiped, true, visuals);
            } else if (!visuals.empty()) {
                QueueFirstPersonRetry(a_actor, a_sources, a_generation);
            }
        }

        // Newly attached geometry needs the actor's current alpha and refraction state.
        actor->UpdateAlpha();
    }

    void RefreshWithLock(
        const Core::ActorKey a_actor,
        const std::vector<AttachmentSource>& a_sources,
        const std::uint64_t a_generation
    ) {
        std::scoped_lock const lock(g_lock);
        if (!IsCurrentRefreshGenerationLocked(a_actor, a_generation)) {
            return;
        }

        RebuildActorAttachments(a_actor, a_sources, a_generation);
    }
}

void EnableFirstPersonRingSlotForRaces() {
    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler) {
        SKSE::log::warn("Visuals: first-person ring race flag patch skipped | reason=noDataHandler");
        return;
    }

    std::uint32_t patched = 0;
    for (auto* race : dataHandler->GetFormArray<RE::TESRace>()) {
        if (!race) {
            continue;
        }

        auto& firstPersonFlags = race->bipedModelData.bipedObjectSlots;
        if (!firstPersonFlags.all(RE::BGSBipedObjectForm::FirstPersonFlag::kRing)) {
            firstPersonFlags.set(RE::BGSBipedObjectForm::FirstPersonFlag::kRing);
            ++patched;
        }
    }

    SKSE::log::info("Visuals: first-person ring race flags patched | races={}", patched);
}

bool RetargetVanillaRingClone(RE::BipedAnim* a_biped, RE::NiAVObject* a_object, const std::int32_t a_bipedObjectSlot) {
    if (!a_biped || !a_object) {
        return false;
    }

    auto const* ring = GetPlayerRingForBipedObjectSlot(*a_biped, a_bipedObjectSlot);
    if (!ring) {
        return false;
    }

    const auto sourceTargets = SourceModelFootprints::GetRingGeometrySourceTargets(*ring);
    if (!NeedsVanillaRingCloneRetarget(*ring, sourceTargets)) {
        return false;
    }

    if (!PatchScenegraph(*a_biped, *a_object, Core::kVanillaRingSlotTarget, sourceTargets)) {
        SKSE::log::warn(
            "Visuals: worn ring retarget failed | source={:08X} | bipedSlot={} | target={} | reason=noEditableRingData",
            ring->GetFormID(),
            a_bipedObjectSlot,
            Core::TargetName(Core::kVanillaRingSlotTarget)
        );
        return false;
    }

    return true;
}

void RequestRefresh(const Core::ActorKey a_actor, std::vector<AttachmentSource> a_sources) {
    if (!a_actor) {
        return;
    }

    std::uint64_t generation {0};
    {
        std::scoped_lock const lock(g_lock);
        generation = ++g_nextRefreshGeneration;
        RefreshGenerations()[a_actor] = generation;
    }

    SKSE::GetTaskInterface()->AddTask([a_actor, sources = std::move(a_sources), generation] {
        RefreshWithLock(a_actor, sources, generation);
    });
}

void Revert() {
    std::scoped_lock const lock(g_lock);
    ++g_nextRefreshGeneration;
    RefreshGenerations().clear();
    FirstPersonRetry::Cancel();

    auto const actors = std::move(TrackedActors());
    TrackedActors().clear();

    for (const auto actorKey : actors) {
        auto const* actor = Core::ResolveActor(actorKey);
        if (!actor) {
            continue;
        }

        const auto& thirdPersonBiped = actor->GetBiped(false);
        if (thirdPersonBiped) {
            DetachExistingRoot(*thirdPersonBiped);
        }

        if (actor->IsPlayerRef()) {
            const auto& firstPersonBiped = actor->GetBiped(true);
            if (firstPersonBiped) {
                DetachExistingRoot(*firstPersonBiped);
            }
        }
    }
}
}
