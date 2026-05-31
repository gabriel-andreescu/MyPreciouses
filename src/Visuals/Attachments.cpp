#include "Visuals/Attachments.h"

#include "Core/Target.h"
#include "ModelPaths.h"
#include "SourceModelFootprints.h"

#include <RE/M/MemoryManager.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace Visuals::Attachments {
namespace {
    constexpr auto* kRootNodeName = "LHRSVirtualRings";
    constexpr auto kLeftHandNode = "NPC L Hand [LHnd]"sv;
    constexpr auto kRightHandNode = "NPC R Hand [RHnd]"sv;
    constexpr std::array<RE::SEX, RE::SEXES::kTotal> kSexes {RE::SEXES::kMale, RE::SEXES::kFemale};

    std::mutex g_lock;
    std::mutex g_firstPersonRetryLock;
    bool g_firstPersonRetryQueued {false};
    Core::ActorKey g_firstPersonRetryActor;
    std::vector<AttachmentSource> g_firstPersonRetrySources;

    [[nodiscard]] std::unordered_set<Core::ActorKey>& TrackedActors() {
        static auto* actors = new std::unordered_set<Core::ActorKey>();
        return *actors;
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
        Core::FingerMask sourceFingerMask;
        RE::FormID sourceFormID {0};
        std::vector<ArmorAddonVisual> addonVisuals;
    };

    struct SkinPatchResult {
        bool foundSkin {false};
        bool retargetedSkin {false};
    };

    [[nodiscard]] std::size_t NiNodeAllocationSize() {
        return REL::Module::IsVR() ? 0x150 : 0x128;
    }

    [[nodiscard]] RE::NiNode* CreateVisualNode(const std::uint16_t a_arrBufLen = 0) {
        auto* node = RE::malloc<RE::NiNode>(NiNodeAllocationSize());
        if (!node) {
            return nullptr;
        }

        std::memset(static_cast<void*>(node), 0, NiNodeAllocationSize());

        using NiNodeCtor_t = RE::NiNode* (*)(RE::NiNode*, std::uint16_t);
        static REL::Relocation<NiNodeCtor_t> ctor {RELOCATION_ID(68936, 70287)};
        return ctor(node, a_arrBufLen);
    }

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
        RE::TESRace& a_actorRace,
        const bool a_customRingSlots
    ) {
        if (!a_customRingSlots && !a_sourceAddon.HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kRing)) {
            return std::nullopt;
        }

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
        const auto customRingSlots = !a_source.HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kRing);
        for (auto* addon : a_source.armorAddons) {
            if (!addon) {
                continue;
            }

            if (auto visual = CaptureArmorAddonVisual(*addon, a_target, a_actorRace, customRingSlots)) {
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

            auto* sourceArmor = RE::TESForm::LookupByID<RE::TESObjectARMO>(source.sourceFormID);
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
                    .sourceFingerMask = source.sourceFingerMask,
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

    [[nodiscard]] std::optional<std::string> GetRetargetedNodeName(
        const Core::Target a_target,
        const Core::FingerMask& a_sourceFingerMask,
        const std::string_view a_name
    ) {
        if (a_name == kLeftHandNode || a_name == kRightHandNode) {
            return a_target.hand == Core::Hand::kLeft ? std::string {kLeftHandNode} : std::string {kRightHandNode};
        }

        return SourceModelFootprints::RetargetFingerBoneName(a_name, a_target, a_sourceFingerMask);
    }

    [[nodiscard]] RE::NiAVObject* FindBone(RE::BipedAnim& a_biped, const char* a_name) {
        if (!a_biped.root || !a_name || a_name[0] == '\0') {
            return nullptr;
        }

        return a_biped.root->GetObjectByName(RE::BSFixedString {a_name});
    }

    [[nodiscard]] RE::NiAVObject* FindRetargetedBone(
        RE::BipedAnim& a_biped,
        const Core::Target a_target,
        const Core::FingerMask& a_sourceFingerMask,
        const RE::NiAVObject& a_bone
    ) {
        const auto* boneName = a_bone.name.c_str();
        if (!boneName || boneName[0] == '\0') {
            return nullptr;
        }

        const auto retargetedName = GetRetargetedNodeName(a_target, a_sourceFingerMask, boneName);
        if (!retargetedName) {
            return nullptr;
        }

        auto* retargetedBone = FindBone(a_biped, retargetedName->c_str());
        return retargetedBone;
    }

    [[nodiscard]] bool RenameRetargetedNodes(
        RE::NiAVObject& a_root,
        const Core::Target a_target,
        const Core::FingerMask& a_sourceFingerMask
    ) {
        bool patched = false;
        RE::BSVisit::TraverseScenegraphObjects(std::addressof(a_root), [&](RE::NiAVObject* a_object) {
            if (!a_object) {
                return RE::BSVisit::BSVisitControl::kContinue;
            }

            const auto* nodeName = a_object->name.c_str();
            if (nodeName && nodeName[0] != '\0') {
                if (const auto retargetedName = GetRetargetedNodeName(a_target, a_sourceFingerMask, nodeName)) {
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
        const Core::Target a_target,
        const Core::FingerMask& a_sourceFingerMask,
        RE::NiSkinInstance& a_skin
    ) {
        bool retargetedThisSkin = false;
        const auto boneCount = a_skin.skinData ? a_skin.skinData->bones : a_skin.numMatrices;
        for (std::uint32_t index = 0; index < boneCount; ++index) {
            const auto* bone = a_skin.bones ? a_skin.bones[index] : nullptr;
            auto* retargetedBone = bone ? FindRetargetedBone(a_biped, a_target, a_sourceFingerMask, *bone) : nullptr;
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
        const Core::Target a_target,
        const Core::FingerMask& a_sourceFingerMask,
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
            result.retargetedSkin = PatchSkinInstance(a_biped, a_target, a_sourceFingerMask, *skin)
                                    || result.retargetedSkin;
            return RE::BSVisit::BSVisitControl::kContinue;
        });
        return result;
    }

    [[nodiscard]] bool PatchScenegraph(
        RE::BipedAnim& a_biped,
        RE::NiAVObject& a_root,
        const Core::Target a_target,
        const Core::FingerMask& a_sourceFingerMask
    ) {
        if (!a_biped.root) {
            return false;
        }

        const auto skinPatch = PatchSkinBindings(a_biped, a_target, a_sourceFingerMask, a_root);
        const auto nodeNamesPatched = RenameRetargetedNodes(a_root, a_target, a_sourceFingerMask);
        return skinPatch.foundSkin ? skinPatch.retargetedSkin : nodeNamesPatched;
    }

    void ApplyTextureSwap(const ModelVariant& a_model, RE::NiAVObject& a_root) {
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
        const Core::FingerMask& a_sourceFingerMask,
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
            logger::warn(
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
            logger::warn(
                "Visuals: deep copy failed | target={} | source={:08X} | addon={:08X} | path='{}' | reason=noNiNode",
                Core::TargetName(a_visual.target),
                a_sourceFormID,
                a_visual.sourceAddon ? a_visual.sourceAddon->GetFormID() : 0,
                model->path
            );
            return nullptr;
        }

        ApplyTextureSwap(*model, *node);
        if (!SourceModelFootprints::IsRingModel(*node)) {
            logger::debug(
                "Visuals: source skipped | target={} | source={:08X} | addon={:08X} | path='{}' | reason=noRingPartition",
                Core::TargetName(a_visual.target),
                a_sourceFormID,
                a_visual.sourceAddon ? a_visual.sourceAddon->GetFormID() : 0,
                model->path
            );
            return nullptr;
        }

        if (!PatchScenegraph(a_biped, *node, a_visual.target, a_sourceFingerMask)) {
            logger::warn(
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
        const AttachmentSourceVisuals& a_visuals
    ) {
        RE::NiPointer<RE::NiNode> targetNode {CreateVisualNode()};
        if (!targetNode) {
            return nullptr;
        }

        const auto targetName = std::string {Core::TargetName(a_visuals.target)};
        targetNode->name = RE::BSFixedString {targetName.c_str()};
        for (const auto& visual : a_visuals.addonVisuals) {
            auto addonNode = BuildAddonVisualNode(
                a_actor,
                a_biped,
                a_firstPerson,
                a_visuals.sourceFormID,
                a_visuals.sourceFingerMask,
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

        RE::NiPointer<RE::NiNode> root {CreateVisualNode()};
        if (!root) {
            return;
        }

        root->name = RE::BSFixedString {kRootNodeName};
        for (const auto& visual : a_visuals) {
            auto targetNode = BuildTargetNode(a_actor, a_biped, a_firstPerson, visual);
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

    void QueueFirstPersonRetry(const Core::ActorKey a_actor, std::vector<AttachmentSource> a_sources) {
        bool shouldQueueRetry = false;
        {
            std::scoped_lock lock(g_firstPersonRetryLock);
            g_firstPersonRetryActor = a_actor;
            g_firstPersonRetrySources = std::move(a_sources);
            shouldQueueRetry = !g_firstPersonRetryQueued;
            g_firstPersonRetryQueued = true;
        }

        if (!shouldQueueRetry) {
            return;
        }

        stl::add_thread_task(
            [] {
                Core::ActorKey actor;
                std::vector<AttachmentSource> sources;
                {
                    std::scoped_lock lock(g_firstPersonRetryLock);
                    actor = g_firstPersonRetryActor;
                    sources = std::move(g_firstPersonRetrySources);
                    g_firstPersonRetryActor = {};
                    g_firstPersonRetryQueued = false;
                }

                RequestRefresh(actor, std::move(sources));
            },
            500ms
        );
    }

    void RebuildActorAttachments(const Core::ActorKey a_actor, const std::vector<AttachmentSource>& a_sources) {
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

        if (!actor->IsPlayerRef()) {
            return;
        }

        const auto& firstPersonBiped = actor->GetBiped(true);
        if (firstPersonBiped) {
            RebuildBipedAttachments(*actor, *firstPersonBiped, true, visuals);
        } else if (!visuals.empty()) {
            QueueFirstPersonRetry(a_actor, a_sources);
        }
    }

    void RefreshWithLock(const Core::ActorKey a_actor, const std::vector<AttachmentSource>& a_sources) {
        std::scoped_lock lock(g_lock);
        RebuildActorAttachments(a_actor, a_sources);
    }
}

void EnableFirstPersonRingSlotForRaces() {
    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler) {
        logger::warn("Visuals: first-person ring race flag patch skipped | reason=noDataHandler");
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

    logger::info("Visuals: first-person ring race flags patched | races={}", patched);
}

void RequestRefresh(const Core::ActorKey a_actor, std::vector<AttachmentSource> a_sources) {
    if (!a_actor) {
        return;
    }

    stl::add_task([a_actor, sources = std::move(a_sources)] {
        RefreshWithLock(a_actor, sources);
    });
}

void Revert() {
    std::scoped_lock lock(g_lock);
    {
        std::scoped_lock retryLock(g_firstPersonRetryLock);
        g_firstPersonRetryActor = {};
        g_firstPersonRetrySources.clear();
        g_firstPersonRetryQueued = false;
    }

    auto actors = std::move(TrackedActors());
    TrackedActors().clear();

    for (const auto actorKey : actors) {
        auto* actor = Core::ResolveActor(actorKey);
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
