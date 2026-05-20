#include "MeshRetargeting.h"

#include "RuntimeEquipment.h"
#include "Slots.h"

#include <algorithm>
#include <limits>
#include <optional>

namespace MeshRetargeting {
namespace {
    constexpr auto kRightHandNode = "NPC R Hand [RHnd]"sv;
    constexpr auto kLeftHandNode = "NPC L Hand [LHnd]"sv;
    constexpr auto kRightFingerNodePrefix = "NPC R Finger"sv;
    constexpr auto kLeftFingerNodePrefix = "NPC L Finger"sv;
    constexpr auto kRightFingerCodePrefix = " [RF"sv;
    constexpr auto kLeftFingerCodePrefix = " [LF"sv;
    constexpr auto kNodeCodeSuffix = "]"sv;
    constexpr auto kBondRightFinger10 = "NPC R Finger10 [RF10]"sv;
    constexpr auto kBondLeftFinger30 = "NPC L Finger30 [LF30]"sv;
    constexpr auto kBondRightFinger11 = "NPC R Finger11 [RF11]"sv;
    constexpr auto kBondLeftFinger31 = "NPC L Finger31 [LF31]"sv;
    constexpr auto kBondRightFinger12 = "NPC R Finger12 [RF12]"sv;
    constexpr auto kBondLeftFinger32 = "NPC L Finger32 [LF32]"sv;

    struct ReplacementNode {
        RE::NiPointer<RE::NiObject> object;
        RE::NiAVObject* node {nullptr};
        bool patched {false};
    };

    struct ModelSelection {
        const RuntimeEquipment::AddonModel::Model* model {nullptr};
        RE::SEX requestedSex {RE::SEXES::kMale};
        RE::SEX selectedSex {RE::SEXES::kMale};
        bool requestedFirstPerson {false};
        bool selectedFirstPerson {false};
    };

    [[nodiscard]] RE::SEX OppositeSex(const RE::SEX a_sex) {
        return a_sex == RE::SEXES::kFemale ? RE::SEXES::kMale : RE::SEXES::kFemale;
    }

    [[nodiscard]] RE::NiAVObject* FindBone(RE::BipedAnim& a_biped, const char* a_name) {
        if (!a_biped.root || !a_name || a_name[0] == '\0') {
            return nullptr;
        }

        return a_biped.root->GetObjectByName(RE::BSFixedString {a_name});
    }

    [[nodiscard]] bool IsAsciiDigit(const char a_value) {
        return a_value >= '0' && a_value <= '9';
    }

    [[nodiscard]] bool IsNonEmptyAsciiNumber(std::string_view a_value) {
        return !a_value.empty() && std::ranges::all_of(a_value, IsAsciiDigit);
    }

    [[nodiscard]] std::optional<std::string> GetMirroredLeftHandNodeName(std::string_view a_name) {
        if (a_name == kRightHandNode) {
            return std::string {kLeftHandNode};
        }

        if (!a_name.starts_with(kRightFingerNodePrefix)) {
            return std::nullopt;
        }

        const auto remainingName = a_name.substr(kRightFingerNodePrefix.size());
        const auto codeStart = remainingName.find(kRightFingerCodePrefix);
        if (codeStart == std::string_view::npos) {
            return std::nullopt;
        }

        const auto fingerNumber = remainingName.substr(0, codeStart);
        if (!IsNonEmptyAsciiNumber(fingerNumber)) {
            return std::nullopt;
        }

        const auto code = remainingName.substr(codeStart);
        const auto minimumCodeSize = kRightFingerCodePrefix.size() + kNodeCodeSuffix.size();
        if (code.size() < minimumCodeSize || !code.ends_with(kNodeCodeSuffix)) {
            return std::nullopt;
        }

        const auto codeNumberLength = code.size() - minimumCodeSize;
        if (codeNumberLength != fingerNumber.size()) {
            return std::nullopt;
        }

        const auto codeNumber = code.substr(kRightFingerCodePrefix.size(), codeNumberLength);
        if (codeNumber != fingerNumber) {
            return std::nullopt;
        }

        std::string mirroredName {kLeftFingerNodePrefix};
        mirroredName += fingerNumber;
        mirroredName += kLeftFingerCodePrefix;
        mirroredName += fingerNumber;
        mirroredName += kNodeCodeSuffix;
        return mirroredName;
    }

    [[nodiscard]] std::optional<std::string> GetBondLeftHandNodeName(const std::string_view a_name) {
        if (a_name == kBondRightFinger10) {
            return std::string {kBondLeftFinger30};
        }
        if (a_name == kBondRightFinger11) {
            return std::string {kBondLeftFinger31};
        }
        if (a_name == kBondRightFinger12) {
            return std::string {kBondLeftFinger32};
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> GetRetargetedLeftHandNodeName(
        const DisplaySlot a_channel,
        const std::string_view a_name
    ) {
        if (a_channel == DisplaySlot::kBond) {
            return GetBondLeftHandNodeName(a_name);
        }

        return GetMirroredLeftHandNodeName(a_name);
    }

    [[nodiscard]] RE::NiAVObject* FindRetargetedLeftHandBone(
        RE::BipedAnim& a_biped,
        const DisplaySlot a_channel,
        const RE::NiAVObject& a_bone
    ) {
        const auto* boneName = a_bone.name.c_str();
        if (!boneName || boneName[0] == '\0') {
            return nullptr;
        }

        const auto mirroredName = GetRetargetedLeftHandNodeName(a_channel, boneName);
        if (!mirroredName) {
            return nullptr;
        }

        return FindBone(a_biped, mirroredName->c_str());
    }

    [[nodiscard]] const char* NodeName(const RE::NiAVObject* a_node) {
        if (!a_node) {
            return "<null>";
        }

        const auto* name = a_node->name.c_str();
        return name && name[0] != '\0' ? name : "<unnamed>";
    }

    [[nodiscard]] RE::Actor* AsActor(RE::TESObjectREFR* a_ref) {
        return a_ref ? skyrim_cast<RE::Actor*>(a_ref) : nullptr;
    }

    [[nodiscard]] bool IsFirstPersonBiped(
        const RE::TESObjectREFR* a_actor,
        const RE::BSTSmartPointer<RE::BipedAnim>& a_biped
    ) {
        if (!a_actor || !a_biped) {
            return false;
        }

        const auto& firstPersonBiped = a_actor->GetBiped(true);
        return firstPersonBiped && firstPersonBiped.get() == a_biped.get();
    }

    [[nodiscard]] RE::SEX GetActorSex(RE::TESObjectREFR* a_actor) {
        const auto* actor = AsActor(a_actor);
        const auto* actorBase = actor ? actor->GetActorBase() : nullptr;
        return actorBase ? actorBase->GetSex() : RE::SEXES::kMale;
    }

    [[nodiscard]] bool IsValidBipedObjectSlot(const std::int32_t a_slot) {
        if (a_slot < 0) {
            return false;
        }

        return static_cast<std::uint32_t>(a_slot) < std::to_underlying(RE::BIPED_OBJECTS::kTotal);
    }

    [[nodiscard]] const RuntimeEquipment::AddonModel::Model& GetModel(
        const RuntimeEquipment::AddonModel& a_metadata,
        const RE::SEX a_sex,
        const bool a_firstPerson
    ) {
        const auto sexIndex = std::to_underlying(a_sex);
        return a_firstPerson ? a_metadata.firstPerson[sexIndex] : a_metadata.thirdPerson[sexIndex];
    }

    [[nodiscard]] std::optional<ModelSelection> SelectCandidateModel(
        const RuntimeEquipment::AddonModel& a_metadata,
        const RE::SEX a_requestedSex,
        const RE::SEX a_selectedSex,
        const bool a_requestedFirstPerson,
        const bool a_selectedFirstPerson
    ) {
        const auto& model = GetModel(a_metadata, a_selectedSex, a_selectedFirstPerson);
        if (model.path.empty()) {
            return std::nullopt;
        }

        return ModelSelection {
            .model = std::addressof(model),
            .requestedSex = a_requestedSex,
            .selectedSex = a_selectedSex,
            .requestedFirstPerson = a_requestedFirstPerson,
            .selectedFirstPerson = a_selectedFirstPerson,
        };
    }

    [[nodiscard]] std::optional<ModelSelection> SelectRuntimeModel(
        const RuntimeEquipment::AddonModel& a_metadata,
        RE::TESObjectREFR* a_actor,
        const RE::BSTSmartPointer<RE::BipedAnim>& a_biped
    ) {
        const auto sex = GetActorSex(a_actor);
        const auto oppositeSex = OppositeSex(sex);
        const auto firstPerson = IsFirstPersonBiped(a_actor, a_biped);

        if (auto selection = SelectCandidateModel(a_metadata, sex, sex, firstPerson, firstPerson)) {
            return selection;
        }

        if (auto selection = SelectCandidateModel(a_metadata, sex, sex, firstPerson, !firstPerson)) {
            return selection;
        }

        if (auto selection = SelectCandidateModel(a_metadata, sex, oppositeSex, firstPerson, firstPerson)) {
            return selection;
        }

        if (auto selection = SelectCandidateModel(a_metadata, sex, oppositeSex, firstPerson, !firstPerson)) {
            return selection;
        }

        return std::nullopt;
    }

    [[nodiscard]] bool RenameRightHandNodes(RE::NiAVObject& a_root, const DisplaySlot a_channel) {
        bool patched = false;
        RE::BSVisit::TraverseScenegraphObjects(std::addressof(a_root), [&](RE::NiAVObject* a_object) {
            if (!a_object) {
                return RE::BSVisit::BSVisitControl::kContinue;
            }

            const auto* nodeName = a_object->name.c_str();
            if (nodeName && nodeName[0] != '\0') {
                if (const auto mirroredName = GetRetargetedLeftHandNodeName(a_channel, nodeName)) {
                    a_object->name = RE::BSFixedString {mirroredName->c_str()};
                    patched = true;
                }
            }

            return RE::BSVisit::BSVisitControl::kContinue;
        });
        return patched;
    }

    [[nodiscard]] bool PatchDismemberPartitions(RE::BSDismemberSkinInstance& a_skin, DisplaySlot a_channel);

    [[nodiscard]] bool PatchSkinInstance(
        RE::BipedAnim& a_biped,
        const DisplaySlot a_channel,
        RE::NiSkinInstance& a_skin
    ) {
        bool patched = false;
        bool retargetedThisSkin = false;
        const auto boneCount = a_skin.skinData ? a_skin.skinData->bones : a_skin.numMatrices;
        for (std::uint32_t index = 0; index < boneCount; ++index) {
            const auto* bone = a_skin.bones ? a_skin.bones[index] : nullptr;
            auto* mirroredBone = bone ? FindRetargetedLeftHandBone(a_biped, a_channel, *bone) : nullptr;
            if (!mirroredBone) {
                continue;
            }

            a_skin.bones[index] = mirroredBone;
            if (a_skin.boneWorldTransforms) {
                a_skin.boneWorldTransforms[index] = std::addressof(mirroredBone->world);
            }

            retargetedThisSkin = true;
            patched = true;
        }

        if (auto* dismemberSkin = netimmerse_cast<RE::BSDismemberSkinInstance*>(std::addressof(a_skin))) {
            patched = PatchDismemberPartitions(*dismemberSkin, a_channel) || patched;
        }

        if (retargetedThisSkin) {
            a_skin.frameID = std::numeric_limits<std::uint32_t>::max();
        }

        return patched;
    }

    [[nodiscard]] bool PatchSkinBindings(RE::BipedAnim& a_biped, const DisplaySlot a_channel, RE::NiAVObject& a_root) {
        bool patched = false;
        RE::BSVisit::TraverseScenegraphGeometries(std::addressof(a_root), [&](RE::BSGeometry* a_geometry) {
            if (!a_geometry) {
                return RE::BSVisit::BSVisitControl::kContinue;
            }

            auto& geometryData = a_geometry->GetGeometryRuntimeData();
            auto* skin = geometryData.skinInstance.get();
            if (!skin) {
                return RE::BSVisit::BSVisitControl::kContinue;
            }

            patched = PatchSkinInstance(a_biped, a_channel, *skin) || patched;
            return RE::BSVisit::BSVisitControl::kContinue;
        });
        return patched;
    }

    [[nodiscard]] bool PatchDismemberPartitions(RE::BSDismemberSkinInstance& a_skin, const DisplaySlot a_channel) {
        auto& runtimeData = a_skin.GetRuntimeData();
        if (!runtimeData.partitions || runtimeData.numPartitions <= 0) {
            return false;
        }

        const auto dismemberPartID = Slots::GetDismemberPartID(a_channel);
        bool patchedAny = false;
        for (auto index = 0; index < runtimeData.numPartitions; ++index) {
            auto& partition = runtimeData.partitions[index];
            if (partition.slot != dismemberPartID) {
                partition.slot = dismemberPartID;
                patchedAny = true;
            }
        }

        if (patchedAny) {
            a_skin.UpdateDismemberPartion(dismemberPartID, true);
        }
        return patchedAny;
    }

    [[nodiscard]] bool PatchScenegraph(RE::BipedAnim& a_biped, RE::NiAVObject& a_root, const DisplaySlot a_channel) {
        if (!a_biped.root) {
            return false;
        }

        const auto skinPatched = PatchSkinBindings(a_biped, a_channel, a_root);
        const auto nodeNamesPatched = RenameRightHandNodes(a_root, a_channel);
        return skinPatched || nodeNamesPatched;
    }

    void ApplyTextureSwap(const RuntimeEquipment::AddonModel::Model& a_model, RE::NiAVObject& a_root) {
        if (!a_model.textureSwap || a_model.numAlternateTextures == 0) {
            return;
        }

        using ApplyTextureSwap_t = void (*)(RE::TESModelTextureSwap*, RE::NiAVObject*) noexcept;
        static REL::Relocation<ApplyTextureSwap_t> applyTextureSwap {RELOCATION_ID(14660, 14837)};
        applyTextureSwap(const_cast<RE::TESModelTextureSwap*>(a_model.textureSwap), std::addressof(a_root));
    }

    [[nodiscard]] std::optional<ReplacementNode> BuildReplacementNode(
        RE::TESObjectREFR* a_actor,
        RE::BSTSmartPointer<RE::BipedAnim>& a_biped,
        RE::TESObjectARMO& a_armor,
        const RE::TESObjectARMA* a_addon,
        const RuntimeEquipment::AddonModel& a_metadata
    ) {
        const auto selection = SelectRuntimeModel(a_metadata, a_actor, a_biped);
        if (!selection || !selection->model) {
            return std::nullopt;
        }

        RE::NiPointer<RE::NiNode> sourceRoot;
        const RE::BSModelDB::DBTraits::ArgsType args;
        const auto loadResult = RE::BSModelDB::Demand(selection->model->path.c_str(), sourceRoot, args);
        if (loadResult != RE::BSResource::ErrorCode::kNone || !sourceRoot) {
            logger::warn(
                "MeshRetargeting: source load failed | armor={:08X} | addon={:08X} | path='{}' | error={}",
                a_armor.GetFormID(),
                a_addon ? a_addon->GetFormID() : 0,
                selection->model->path,
                std::to_underlying(loadResult)
            );
            return std::nullopt;
        }

        ReplacementNode replacement;
        sourceRoot->CreateDeepCopy(replacement.object);
        replacement.node = replacement.object ? replacement.object->AsNode() : nullptr;
        if (!replacement.node) {
            logger::warn(
                "MeshRetargeting: clone failed | armor={:08X} | addon={:08X} | path='{}' | reason=noNiNode",
                a_armor.GetFormID(),
                a_addon ? a_addon->GetFormID() : 0,
                selection->model->path
            );
            return std::nullopt;
        }

        ApplyTextureSwap(*selection->model, *replacement.node);
        replacement.patched = PatchScenegraph(*a_biped, *replacement.node, a_metadata.channel);
        if (!replacement.patched) {
            logger::warn(
                "MeshRetargeting: patch failed | armor={:08X} | addon={:08X} | path='{}' | reason=noEditableRingData",
                a_armor.GetFormID(),
                a_addon ? a_addon->GetFormID() : 0,
                selection->model->path
            );
            return std::nullopt;
        }

        return replacement;
    }

    void UpdateAttachedNode(RE::NiAVObject& a_node, RE::NiNode* a_parent) {
        RE::NiUpdateData updateData {};
        updateData.flags.set(RE::NiUpdateData::Flag::kDirty);
        a_node.UpdateDownwardPass(updateData, 0);
        if (a_parent) {
            a_parent->UpdateDownwardPass(updateData, 0);
        }
    }

    void ReplaceRuntimePartClone(const AttachContext& a_context) {
        auto actor = a_context.actorHandle.get();
        auto* actorRef = actor.get();
        if (!a_context.biped || !a_context.armor) {
            return;
        }

        if (!IsValidBipedObjectSlot(a_context.slot)) {
            return;
        }

        auto& bipedObject = a_context.biped->objects[a_context.slot];
        if (bipedObject.item != a_context.armor || bipedObject.addon != a_context.addon) {
            return;
        }

        const auto metadata = RuntimeEquipment::GetAddonModel(a_context.armor, a_context.addon);
        if (!metadata) {
            return;
        }

        auto replacement = BuildReplacementNode(
            actorRef,
            const_cast<RE::BSTSmartPointer<RE::BipedAnim>&>(a_context.biped),
            *a_context.armor,
            a_context.addon,
            *metadata
        );
        if (!replacement || !replacement->node) {
            logger::warn(
                "MeshRetargeting: replacement failed | armor={:08X} | addon={:08X} | slot={} | partClone={} | node='{}'",
                a_context.armor->GetFormID(),
                a_context.addon ? a_context.addon->GetFormID() : 0,
                a_context.slot,
                bipedObject.partClone ? "yes" : "no",
                NodeName(bipedObject.partClone.get())
            );
            return;
        }

        auto oldPartClone = bipedObject.partClone;
        auto* parent = oldPartClone ? oldPartClone->parent : a_context.biped->root;
        if (!parent) {
            logger::warn(
                "MeshRetargeting: replacement failed | armor={:08X} | addon={:08X} | slot={} | oldPartClone={} | node='{}' | reason=noParent",
                a_context.armor->GetFormID(),
                a_context.addon ? a_context.addon->GetFormID() : 0,
                a_context.slot,
                oldPartClone ? "yes" : "no",
                NodeName(oldPartClone.get())
            );
            return;
        }

        RE::NiPointer<RE::NiAVObject> detached;
        if (oldPartClone && oldPartClone->parent == parent) {
            parent->DetachChild(oldPartClone.get(), detached);
        }

        replacement->node->SetAppCulled(false);
        parent->AttachChild(replacement->node, true);
        bipedObject.partClone = RE::NiPointer<RE::NiAVObject> {replacement->node};
        UpdateAttachedNode(*replacement->node, parent);

        (void)detached;
    }

}

std::optional<AttachContext> CaptureAttachContext(
    RE::NiAVObject* a_clonedNode,
    RE::NiAVObject* a_node,
    const std::int32_t a_slot,
    RE::TESObjectREFR* a_actor,
    RE::BSTSmartPointer<RE::BipedAnim>& a_biped
) {
    (void)a_clonedNode;
    (void)a_node;

    if (!IsValidBipedObjectSlot(a_slot) || !a_biped) {
        return std::nullopt;
    }

    auto& bipedObject = a_biped->objects[a_slot];
    auto* armor = bipedObject.item ? bipedObject.item->As<RE::TESObjectARMO>() : nullptr;
    auto* addon = bipedObject.addon;

    auto metadata = RuntimeEquipment::GetAddonModel(armor, addon);
    if (!metadata) {
        return std::nullopt;
    }

    auto* actor = AsActor(a_actor);

    return AttachContext {
        .actorHandle = actor ? actor->GetHandle() : RE::ActorHandle {},
        .biped = a_biped,
        .armor = armor,
        .addon = addon,
        .slot = a_slot,
    };
}

void QueueReplacement(AttachContext a_context) {
    stl::add_task([a_context = std::move(a_context)] {
        ReplaceRuntimePartClone(a_context);
    });
}
}
