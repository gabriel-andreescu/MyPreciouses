#include "DevBenchInspection.h"

#include "Core/ActorKey.h"
#include "Core/Target.h"
#include "Equipment/AssignmentStore.h"
#include "Inventory.h"
#include "Papyrus/ScriptEventMirror.h"
#include "SourceModelFootprints.h"
#include "UI/FingerSelectMenu.h"
#include "Visuals/Attachments.h"

#include <RE/Skyrim.h> // IWYU pragma: keep

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

namespace {
std::string InspectArmor(const RE::TESObjectARMO& a_armor, int a_count) {
    const auto targets = SourceModelFootprints::GetRingGeometrySourceTargets(a_armor);
    std::string projections = "[";
    for (const auto target : Core::kAllTargets) {
        if (projections.size() > 1) {
            projections += ',';
        }
        projections += std::to_string(SourceModelFootprints::GetProjectedTargets(targets, target).Bits());
    }
    return std::format(
        R"({{"formId":{},"count":{},"isRing":{},"hasRingModel":{},"sourceTargets":{},"projectedTargets":{}]}})",
        a_armor.GetFormID(),
        a_count,
        Inventory::IsRing(&a_armor),
        SourceModelFootprints::HasRingModel(a_armor),
        targets.Bits(),
        projections
    );
}

std::string InspectInventory(RE::Actor& a_actor) {
    std::string result = "[";
    for (const auto& [form, entry] : a_actor.GetInventory()) {
        const auto* armor = form->As<RE::TESObjectARMO>();
        if (armor == nullptr || entry.first <= 0) {
            continue;
        }
        if (result.size() > 1) {
            result += ',';
        }
        result += InspectArmor(*armor, entry.first);
    }
    return result + ']';
}

std::string InspectAssignments() {
    std::string result = "[";
    for (const auto& snapshot : Equipment::AssignmentStore::GetAllSnapshots()) {
        for (const auto target : Core::kAllTargets) {
            const auto& assignment = snapshot.assignments.byTarget[Core::ToIndex(target)];
            if (!assignment.IsAssigned()) {
                continue;
            }
            if (result.size() > 1) {
                result += ',';
            }
            result += std::format(
                R"({{"actor":{},"target":{},"source":{},"kind":{},"enchantment":{},"effectSource":{},"uniqueId":{}}})",
                snapshot.actor.referenceFormID,
                Core::ToIndex(target),
                assignment.source.sourceFormID,
                static_cast<std::uint32_t>(assignment.source.kind),
                assignment.source.customEnchantment.enchantmentFormID,
                assignment.retainedEffectSourceFormID,
                assignment.source.extraUniqueID ? assignment.source.extraUniqueID->uniqueID : 0
            );
        }
    }
    return result + ']';
}

std::string InspectScriptBindings() {
    std::string result = "[";
    for (const auto& binding : Papyrus::ScriptEventMirror::GetBindingSnapshots()) {
        if (result.size() > 1) {
            result += ',';
        }
        result += std::format(
            R"({{"actor":{},"source":{},"effectSource":{},"suspended":{}}})",
            binding.actor.referenceFormID,
            binding.sourceFormID,
            binding.effectSourceFormID,
            binding.suspended
        );
    }
    return result + ']';
}

struct GeometrySnapshot {
    std::uint32_t count {0};
    std::string shaders {"[]"};
};

GeometrySnapshot InspectAttachedGeometry(RE::Actor const& a_actor, bool a_firstPerson, Core::Target a_target) {
    const auto& biped = a_actor.GetBiped(a_firstPerson);
    if (!biped || !biped->root) {
        return {};
    }
    RE::NiAVObject* target = nullptr;
    if (a_target == Core::kVanillaRingSlotTarget) {
        target = biped->objects[RE::BIPED_OBJECTS::kRing].partClone.get();
    } else {
        auto* root = biped->root->GetObjectByName(Visuals::Attachments::kRootNodeName);
        target = root ? root->GetObjectByName(RE::BSFixedString {Core::TargetName(a_target)}) : nullptr;
    }
    if (target == nullptr) {
        return {};
    }
    GeometrySnapshot snapshot;
    snapshot.shaders = "[";
    RE::BSVisit::TraverseScenegraphGeometries(target, [&snapshot](RE::BSGeometry* a_geometry) {
        ++snapshot.count;
        if (const auto& property = a_geometry->GetGeometryRuntimeData().shaderProperty) {
            if (snapshot.shaders.size() > 1) {
                snapshot.shaders += ',';
            }
            using Flag = RE::BSShaderProperty::EShaderPropertyFlag;
            snapshot.shaders += std::format(
                R"({{"alpha":{},"materialAlpha":{},"refraction":{},"temporaryRefraction":{}}})",
                property->alpha,
                property->QMaterialAlpha(),
                property->flags.any(Flag::kRefraction),
                property->flags.any(Flag::kTempRefraction)
            );
        }
        return RE::BSVisit::BSVisitControl::kContinue;
    });
    snapshot.shaders += ']';
    return snapshot;
}

std::string InspectVisuals(RE::Actor const& a_actor) {
    std::string result = "[";
    for (const auto target : Core::kAllTargets) {
        if (result.size() > 1) {
            result += ',';
        }
        const auto thirdPerson = InspectAttachedGeometry(a_actor, false, target);
        const auto firstPerson = a_actor.IsPlayerRef() ? InspectAttachedGeometry(a_actor, true, target)
                                                       : GeometrySnapshot {};
        result += std::format(
            R"({{"target":{},"thirdPersonGeometry":{},"firstPersonGeometry":{},"thirdPersonShaders":{},"firstPersonShaders":{}}})",
            Core::ToIndex(target),
            thirdPerson.count,
            firstPerson.count,
            thirdPerson.shaders,
            firstPerson.shaders
        );
    }
    return result + ']';
}

std::string InspectEffects(RE::Actor& a_actor) {
    std::string result = "[";
    if (auto* const effects = a_actor.AsMagicTarget()->GetActiveEffectList()) { // NOLINT(misc-const-correctness)
        for (const auto* effect : *effects) {
            if (effect == nullptr) {
                continue;
            }
            if (result.size() > 1) {
                result += ',';
            }
            const auto* base = effect->GetBaseObject();
            result += std::format(
                R"({{"effect":{},"source":{},"spell":{},"magnitude":{},"inactive":{},"dispelled":{}}})",
                base ? base->GetFormID() : 0,
                effect->source ? effect->source->GetFormID() : 0,
                effect->spell ? effect->spell->GetFormID() : 0,
                effect->GetMagnitude(),
                effect->flags.any(RE::ActiveEffect::Flag::kInactive),
                effect->flags.any(RE::ActiveEffect::Flag::kDispelled)
            );
        }
    }
    return result + ']';
}

std::string InspectActors() {
    std::vector<Core::ActorKey> actors {Core::GetPlayerActorKey()};
    for (const auto& snapshot : Equipment::AssignmentStore::GetAllSnapshots()) {
        if (!std::ranges::contains(actors, snapshot.actor)) {
            actors.push_back(snapshot.actor);
        }
    }
    std::string result = "[";
    for (const auto key : actors) {
        auto* actor = Core::ResolveActor(key);
        if (actor == nullptr) {
            continue;
        }
        if (result.size() > 1) {
            result += ',';
        }
        const auto worn = Inventory::FindRightWornRing(*actor);
        result += std::format(
            R"({{"formId":{},"race":{},"rightWorn":{},"visuals":{},"effects":{}}})",
            actor->GetFormID(),
            actor->GetRace() ? actor->GetRace()->GetFormID() : 0,
            worn ? worn->ring->GetFormID() : 0,
            InspectVisuals(*actor),
            InspectEffects(*actor)
        );
    }
    return result + ']';
}

}

std::string DevBenchInspection::Snapshot() {
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (player == nullptr) {
        return R"({"ok":false,"error":"The player is not loaded"})";
    }
    return std::format(
        R"({{"ok":true,"inventory":{},"assignments":{},"actors":{},"scriptBindings":{},"selectorOpen":{}}})",
        InspectInventory(*player),
        InspectAssignments(),
        InspectActors(),
        InspectScriptBindings(),
        UI::FingerSelectMenu::IsOpen()
    );
}
