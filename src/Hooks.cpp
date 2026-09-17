#include "Hooks.h"
#include "Settings.h"

#include <algorithm>
#include <format>
#include <vector>

namespace {
    thread_local bool forcedGripOperation = false;
    // True only while this hook is forwarding a NORMAL player equip into Skyrim.
    // If Skyrim internally unequips the previously equipped weapon as part of that
    // equip, Unequip2H must not reinterpret that nested unequip as a second-toggle
    // request. Explicit API operations are already protected by forcedGripOperation.
    thread_local bool normalPlayerEquipInProgress = false;

    struct ScopedNormalPlayerEquip {
        ScopedNormalPlayerEquip() : previous(normalPlayerEquipInProgress) {
            normalPlayerEquipInProgress = true;
        }
        ~ScopedNormalPlayerEquip() { normalPlayerEquipInProgress = previous; }
        ScopedNormalPlayerEquip(const ScopedNormalPlayerEquip&) = delete;
        ScopedNormalPlayerEquip& operator=(const ScopedNormalPlayerEquip&) = delete;

    private:
        bool previous;
    };

    constexpr bool IsSupportedConversion(bool isTwoHandedWeapon, bool isOneHandedWeapon,
        DYNAMIC_TWO_HANDED_API::Grip grip) {
        return (grip == DYNAMIC_TWO_HANDED_API::Grip::kOneHanded && isTwoHandedWeapon) ||
            (grip == DYNAMIC_TWO_HANDED_API::Grip::kTwoHanded && isOneHandedWeapon);
    }

    constexpr bool IsSupportedGripRequest(bool isTwoHandedWeapon, bool isOneHandedWeapon,
        DYNAMIC_TWO_HANDED_API::Grip grip) {
        return IsSupportedConversion(isTwoHandedWeapon, isOneHandedWeapon, grip) ||
            (grip == DYNAMIC_TWO_HANDED_API::Grip::kTwoHanded && isTwoHandedWeapon);
    }

    static_assert(IsSupportedConversion(true, false, DYNAMIC_TWO_HANDED_API::Grip::kOneHanded));
    static_assert(IsSupportedConversion(false, true, DYNAMIC_TWO_HANDED_API::Grip::kTwoHanded));
    static_assert(!IsSupportedConversion(true, false, DYNAMIC_TWO_HANDED_API::Grip::kTwoHanded));
    static_assert(IsSupportedGripRequest(true, false, DYNAMIC_TWO_HANDED_API::Grip::kTwoHanded));

    constexpr bool MeetsLevel(int actorLevel, int requiredLevel) {
        return actorLevel >= requiredLevel;
    }

    static_assert(MeetsLevel(10, 10));
    static_assert(!MeetsLevel(9, 10));

    const char* GripName(DYNAMIC_TWO_HANDED_API::Grip grip) {
        switch (grip) {
        case DYNAMIC_TWO_HANDED_API::Grip::kOneHanded:
            return "OneHanded";
        case DYNAMIC_TWO_HANDED_API::Grip::kTwoHanded:
            return "TwoHanded";
        default:
            return "UnknownGrip";
        }
    }

    const char* HandName(DYNAMIC_TWO_HANDED_API::Hand hand) {
        switch (hand) {
        case DYNAMIC_TWO_HANDED_API::Hand::kRight:
            return "Right";
        case DYNAMIC_TWO_HANDED_API::Hand::kLeft:
            return "Left";
        case DYNAMIC_TWO_HANDED_API::Hand::kBoth:
            return "Both";
        default:
            return "UnknownHand";
        }
    }

    const char* KnownSlotName(const RE::BGSEquipSlot* slot) {
        if (!slot) return "nullptr";
        if (slot == Hooks::g_rightHandSlot) return "RightHand";
        if (slot == Hooks::g_leftHandSlot) return "LeftHand";
        if (slot == Hooks::g_twoHandSlot) return "TwoHand";
        if (slot == Hooks::g_shield) return "Shield";
        return "Other";
    }

    const char* RawSlotName(const std::int64_t* slot) {
        if (!slot) return "nullptr";
        if (slot == reinterpret_cast<const std::int64_t*>(Hooks::g_rightHandSlot)) return "RightHand";
        if (slot == reinterpret_cast<const std::int64_t*>(Hooks::g_leftHandSlot)) return "LeftHand";
        if (slot == reinterpret_cast<const std::int64_t*>(Hooks::g_twoHandSlot)) return "TwoHand";
        if (slot == reinterpret_cast<const std::int64_t*>(Hooks::g_shield)) return "Shield";
        return "Other/Unknown";
    }

    std::string FormLabel(const RE::TESForm* form) {
        if (!form) return "<none>";
        const char* editorID = form->GetFormEditorID();
        return std::format("{} [{:08X}]", editorID && editorID[0] ? editorID : "<no-editor-id>", form->GetFormID());
    }

    void LogActorSlots(RE::Actor* actor, std::string_view prefix) {
        if (!actor) {
            logger::debug("[D2H EquipDebug] {} actor=null", prefix);
            return;
        }

        const auto* right = Hooks::g_rightHandSlot ? actor->GetEquippedObjectInSlot(Hooks::g_rightHandSlot) : nullptr;
        const auto* left = Hooks::g_leftHandSlot ? actor->GetEquippedObjectInSlot(Hooks::g_leftHandSlot) : nullptr;
        const auto* two = Hooks::g_twoHandSlot ? actor->GetEquippedObjectInSlot(Hooks::g_twoHandSlot) : nullptr;

        logger::debug(
            "[D2H EquipDebug] {} actor='{}' [{:08X}] slots: Right={} | Left={} | TwoHand={}",
            prefix,
            actor->GetName(),
            actor->GetFormID(),
            FormLabel(right),
            FormLabel(left),
            FormLabel(two));
    }

}

bool Hooks::isTwoHanded(RE::TESForm* a_weap) {
    if (!a_weap || !a_weap->IsWeapon()) return false;
    auto weap = a_weap->As<RE::TESObjectWEAP>();
    if (weap->IsTwoHandedSword() || weap->IsTwoHandedAxe()) return true;
    return false;
}

bool Hooks::isOneHanded(RE::TESForm* a_weap) {
    if (!a_weap || !a_weap->IsWeapon()) return false;
    auto weap = a_weap->As<RE::TESObjectWEAP>();
    // Verifica explicitamente os tipos de 1H
    if (weap->IsOneHandedSword() || weap->IsOneHandedDagger() || weap->IsOneHandedAxe() || weap->IsOneHandedMace()) {
        return true;
    }
    return false;
}

bool isShield(RE::TESForm* a_item) {
    if (!a_item || !a_item->IsArmor()) return false;
    auto armor = a_item->As<RE::TESObjectARMO>();
    return armor->IsShield();
}

void EquipItemWithGripChange(RE::Actor* actor, RE::TESBoundObject* item, RE::BGSEquipSlot* targetSlot) {
    if (!actor || !item || !targetSlot) return;
    auto equipManager = RE::ActorEquipManager::GetSingleton();
    if (!equipManager) return;
    equipManager->EquipObject(actor, item, nullptr, 1, targetSlot);
}

bool can2h(RE::Actor* actor) {
    if (!actor || !actor->IsHumanoid()) return false;

    // Verifica N�vel do Personagem
    int actorLevel = actor->GetLevel();

    // Verifica Valor da Skill TwoHanded
    float skillValue = actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kTwoHanded);

    bool meetsRequirements = MeetsLevel(actorLevel, ModSettings::Settings.minimumLevel) &&
        skillValue >= ModSettings::Settings.skillValue &&
        ModSettings::HasRequiredPerk(actor, ModSettings::Settings.npcRequiredPerk);

    if (!meetsRequirements) {
        logger::debug("  - NPC '{}' falhou nos requisitos: Level {}/{} , Skill 2H {}/{}",
            actor->GetName(), actorLevel, ModSettings::Settings.minimumLevel, skillValue, ModSettings::Settings.skillValue);
    }

    return meetsRequirements;
}

namespace {
    // A second *native two-handed* weapon is a distinct capability from merely
    // holding one 2H weapon in a one-hand slot. NPCs retain their existing
    // level/skill/perk requirements; the player uses the dedicated dual settings.
    bool CanDualTwoHanded(RE::Actor* actor) {
        if (!actor || !actor->IsHumanoid()) return false;
        if (!actor->IsPlayerRef()) return can2h(actor);

        const auto& settings = ModSettings::Settings;
        const bool levelOK = MeetsLevel(actor->GetLevel(), settings.playerDualTwoHandedMinimumLevel);
        const bool perkOK = ModSettings::HasRequiredPerk(actor, settings.playerDualTwoHandedPerk);
        const bool result = levelOK && perkOK;
        logger::debug(
            "[D2H EquipDebug] CanDualTwoHanded actor={:08X} level={}/{} levelOK={} perk={:08X} perkOK={} RESULT={}",
            actor->GetFormID(), actor->GetLevel(), settings.playerDualTwoHandedMinimumLevel,
            levelOK, settings.playerDualTwoHandedPerk, perkOK, result);
        return result;
    }
}

namespace {
    std::int32_t GetItemCount(RE::Actor* actor, RE::TESBoundObject* item) {
        if (!actor || !item) return 0;
        const auto inventory = actor->GetInventoryCounts();
        const auto entry = inventory.find(item);
        return entry != inventory.end() ? entry->second : 0;
    }

    RE::BGSEquipSlot* GripSlot(DYNAMIC_TWO_HANDED_API::Grip grip, DYNAMIC_TWO_HANDED_API::Hand hand) {
        if (grip == DYNAMIC_TWO_HANDED_API::Grip::kTwoHanded) return Hooks::g_twoHandSlot;
        return hand == DYNAMIC_TWO_HANDED_API::Hand::kLeft ? Hooks::g_leftHandSlot : Hooks::g_rightHandSlot;
    }

    bool IsEquippedInSlot(RE::Actor* actor, RE::TESObjectWEAP* weapon, RE::BGSEquipSlot* slot) {
        return actor && weapon && slot && actor->GetEquippedObjectInSlot(slot) == weapon;
    }

    bool IsLeftSlot(const RE::BGSEquipSlot* slot) {
        return slot && slot == Hooks::g_leftHandSlot;
    }

    bool IsRightLikeSlot(const RE::BGSEquipSlot* slot) {
        return slot && (slot == Hooks::g_rightHandSlot || slot == Hooks::g_twoHandSlot);
    }

    bool IsWornForSlot(const RE::ExtraDataList* extra, const RE::BGSEquipSlot* slot) {
        if (!extra || !slot) return false;
        if (IsLeftSlot(slot)) {
            return extra->HasType(RE::ExtraDataType::kWornLeft);
        }
        if (IsRightLikeSlot(slot)) {
            return extra->HasType(RE::ExtraDataType::kWorn);
        }
        return false;
    }

    bool IsWornAnywhere(const RE::ExtraDataList* extra) {
        return extra && (
            extra->HasType(RE::ExtraDataType::kWorn) ||
            extra->HasType(RE::ExtraDataType::kWornLeft));
    }

    std::vector<RE::ExtraDataList*> InventoryExtraLists(RE::Actor* actor, RE::TESObjectWEAP* weapon) {
        std::vector<RE::ExtraDataList*> result;
        if (!actor || !weapon) return result;

        auto inventory = actor->GetInventory();
        for (auto& [object, data] : inventory) {
            if (!object || object != weapon || !data.second) continue;
            auto* entry = data.second.get();
            if (!entry->extraLists) break;
            for (auto* extra : *entry->extraLists) {
                if (extra) result.push_back(extra);
            }
            break;
        }
        return result;
    }

    bool ContainsExtra(const std::vector<RE::ExtraDataList*>& extras, const RE::ExtraDataList* extra) {
        return extra && std::find(extras.begin(), extras.end(), extra) != extras.end();
    }

    RE::ExtraDataList* ResolveExtraForUnequip(
        RE::Actor* actor,
        RE::TESObjectWEAP* weapon,
        RE::ExtraDataList* requested,
        RE::BGSEquipSlot* slot) {
        const auto extras = InventoryExtraLists(actor, weapon);

        // The slot is authoritative. For duplicate base forms the caller often has
        // only an aggregate InventoryEntryData and its first ExtraDataList may belong
        // to the OTHER hand. Always prefer the list carrying the worn marker for the
        // requested hand.
        for (auto* extra : extras) {
            if (IsWornForSlot(extra, slot)) {
                return extra;
            }
        }

        // A caller-supplied pointer is accepted only while it still belongs to this
        // actor/item. Never dereference an arbitrary/stale pointer from an external UI.
        if (ContainsExtra(extras, requested) &&
            (IsWornForSlot(requested, slot) || GetItemCount(actor, weapon) <= 1)) {
            return requested;
        }

        return nullptr;
    }

    RE::ExtraDataList* ResolveExtraForEquip(
        RE::Actor* actor,
        RE::TESObjectWEAP* weapon,
        RE::ExtraDataList* requested,
        RE::BGSEquipSlot* slot) {
        const auto extras = InventoryExtraLists(actor, weapon);

        // Keep an explicitly requested instance only if it is still owned by this
        // inventory and is either free or already belongs to the requested hand.
        if (ContainsExtra(extras, requested) &&
            (!IsWornAnywhere(requested) || IsWornForSlot(requested, slot))) {
            return requested;
        }

        // Prefer a genuinely unused instance. This is what makes dual same-base
        // operations use two physical inventory copies instead of reusing the same
        // ExtraDataList for Right and Left.
        for (auto* extra : extras) {
            if (!IsWornAnywhere(extra)) {
                return extra;
            }
        }

        // nullptr is meaningful to ActorEquipManager: let Skyrim choose from the
        // unextended stack when no per-instance extra data is required/available.
        return nullptr;
    }

    bool IsEquippedInRequestedHand(RE::Actor* actor, RE::TESObjectWEAP* weapon, RE::BGSEquipSlot* slot) {
        // IMPORTANT: the explicit Dynamic2H equip slot is the source of truth.
        // Actor::GetEquippedObject(false/true) is a hand-view used by Skyrim and can
        // report a native TwoHand weapon as a hand object even when Right/Left are
        // actually empty. Using that value here caused a OneHanded/Right unequip to
        // target a weapon that was really in g_twoHandSlot, corrupting the hand view
        // and leaving a phantom Left/Right equipped state.
        return IsEquippedInSlot(actor, weapon, slot);
    }

    bool HasLegacyHandView(RE::Actor* actor, RE::TESObjectWEAP* weapon, RE::BGSEquipSlot* slot) {
        if (!actor || !weapon || !slot) return false;
        if (slot == Hooks::g_leftHandSlot) {
            return actor->GetEquippedObject(true) == weapon;
        }
        if (slot == Hooks::g_rightHandSlot) {
            return actor->GetEquippedObject(false) == weapon;
        }
        return false;
    }

    bool EquipInSlot(RE::Actor* actor, RE::TESObjectWEAP* weapon, RE::ExtraDataList* extra, RE::BGSEquipSlot* slot) {
        auto* manager = RE::ActorEquipManager::GetSingleton();
        if (!manager || !actor || !weapon || !slot) {
            logger::debug(
                "[D2H EquipDebug] EquipInSlot ABORT manager={} actor={} weapon={} slot={}",
                static_cast<bool>(manager),
                static_cast<bool>(actor),
                static_cast<bool>(weapon),
                static_cast<bool>(slot));
            return false;
        }

        auto* originalSlot = weapon->GetEquipSlot();
        logger::debug(
            "[D2H EquipDebug] EquipInSlot ENTER weapon={} requestedSlot={} [{:08X}] originalWeaponSlot={} [{:08X}] extra={}",
            FormLabel(weapon),
            KnownSlotName(slot),
            slot->GetFormID(),
            KnownSlotName(originalSlot),
            originalSlot ? originalSlot->GetFormID() : 0,
            static_cast<const void*>(extra));
        LogActorSlots(actor, "EquipInSlot BEFORE");

        auto* resolvedExtra = ResolveExtraForEquip(actor, weapon, extra, slot);
        logger::debug(
            "[D2H EquipDebug] EquipInSlot extra resolution requested={} resolved={} slot={}",
            static_cast<const void*>(extra),
            static_cast<const void*>(resolvedExtra),
            KnownSlotName(slot));

        weapon->SetEquipSlot(slot);
        const bool previousForcedGripOperation = forcedGripOperation;
        forcedGripOperation = true;
        logger::debug(
            "[D2H EquipDebug] EquipInSlot CALL ActorEquipManager::EquipObject forcedGripOperation=true slot={}",
            KnownSlotName(slot));
        manager->EquipObject(actor, weapon, resolvedExtra, 1, slot);
        logger::debug("[D2H EquipDebug] EquipInSlot RETURN ActorEquipManager::EquipObject");
        forcedGripOperation = previousForcedGripOperation;
        weapon->SetEquipSlot(originalSlot);

        LogActorSlots(actor, "EquipInSlot AFTER");
        logger::debug(
            "[D2H EquipDebug] EquipInSlot EXIT restoredWeaponSlot={} [{:08X}]",
            KnownSlotName(originalSlot),
            originalSlot ? originalSlot->GetFormID() : 0);
        return true;
    }

    bool UnequipInSlot(RE::Actor* actor, RE::TESObjectWEAP* weapon, RE::ExtraDataList* extra, RE::BGSEquipSlot* slot) {
        auto* manager = RE::ActorEquipManager::GetSingleton();
        if (!manager || !actor || !weapon || !slot) {
            logger::debug(
                "[D2H EquipDebug] UnequipInSlot ABORT manager={} actor={} weapon={} slot={}",
                static_cast<bool>(manager),
                static_cast<bool>(actor),
                static_cast<bool>(weapon),
                static_cast<bool>(slot));
            return false;
        }

        auto* originalSlot = weapon->GetEquipSlot();
        logger::debug(
            "[D2H EquipDebug] UnequipInSlot ENTER weapon={} requestedSlot={} [{:08X}] originalWeaponSlot={} [{:08X}] extra={}",
            FormLabel(weapon),
            KnownSlotName(slot),
            slot->GetFormID(),
            KnownSlotName(originalSlot),
            originalSlot ? originalSlot->GetFormID() : 0,
            static_cast<const void*>(extra));
        LogActorSlots(actor, "UnequipInSlot BEFORE");

        if (!IsEquippedInRequestedHand(actor, weapon, slot)) {
            logger::debug(
                "[D2H EquipDebug] UnequipInSlot SKIP weapon is not equipped in requested slot={}",
                KnownSlotName(slot));
            return false;
        }

        auto* resolvedExtra = ResolveExtraForUnequip(actor, weapon, extra, slot);
        logger::debug(
            "[D2H EquipDebug] UnequipInSlot extra resolution requested={} resolved={} slot={}",
            static_cast<const void*>(extra),
            static_cast<const void*>(resolvedExtra),
            KnownSlotName(slot));

        weapon->SetEquipSlot(slot);
        const bool previousForcedGripOperation = forcedGripOperation;
        forcedGripOperation = true;
        logger::debug(
            "[D2H EquipDebug] UnequipInSlot CALL ActorEquipManager::UnequipObject forcedGripOperation=true slot={}",
            KnownSlotName(slot));
        manager->UnequipObject(actor, weapon, resolvedExtra, 1, slot, false, true, true, true);
        logger::debug("[D2H EquipDebug] UnequipInSlot RETURN ActorEquipManager::UnequipObject");
        forcedGripOperation = previousForcedGripOperation;
        weapon->SetEquipSlot(originalSlot);

        LogActorSlots(actor, "UnequipInSlot AFTER");
        logger::debug(
            "[D2H EquipDebug] UnequipInSlot EXIT restoredWeaponSlot={} [{:08X}]",
            KnownSlotName(originalSlot),
            originalSlot ? originalSlot->GetFormID() : 0);
        return true;
    }
}

bool Hooks::CanEquipWithGrip(RE::Actor* actor, RE::TESObjectWEAP* weapon,
    DYNAMIC_TWO_HANDED_API::Grip grip, DYNAMIC_TWO_HANDED_API::Hand hand) {
    logger::debug(
        "[D2H EquipDebug] CanEquipWithGrip ENTER actor={} weapon={} grip={} hand={} forcedGripOperation={}",
        actor ? FormLabel(actor) : "<null>",
        weapon ? FormLabel(weapon) : "<null>",
        GripName(grip),
        HandName(hand),
        forcedGripOperation);

    if (!actor || !weapon || !actor->IsHumanoid()) {
        logger::debug(
            "[D2H EquipDebug] CanEquipWithGrip REJECT basic validation actor={} weapon={} humanoid={}",
            static_cast<bool>(actor),
            static_cast<bool>(weapon),
            actor ? actor->IsHumanoid() : false);
        return false;
    }

    if (grip == DYNAMIC_TWO_HANDED_API::Grip::kTwoHanded && hand != DYNAMIC_TWO_HANDED_API::Hand::kBoth) {
        logger::debug(
            "[D2H EquipDebug] CanEquipWithGrip REJECT TwoHanded grip requires Hand::kBoth; received {}",
            HandName(hand));
        return false;
    }

    const bool twoHandedWeapon = isTwoHanded(weapon);
    const bool oneHandedWeapon = isOneHanded(weapon);
    const bool keepsNativeTwoHanded = grip == DYNAMIC_TWO_HANDED_API::Grip::kTwoHanded && twoHandedWeapon;
    const bool convertsTwoToOne = grip == DYNAMIC_TWO_HANDED_API::Grip::kOneHanded && twoHandedWeapon;
    const bool convertsOneToTwo = grip == DYNAMIC_TWO_HANDED_API::Grip::kTwoHanded && oneHandedWeapon;

    logger::debug(
        "[D2H EquipDebug] CanEquipWithGrip classification is2H={} is1H={} native2H={} 2Hto1H={} 1Hto2H={} itemCount={}",
        twoHandedWeapon,
        oneHandedWeapon,
        keepsNativeTwoHanded,
        convertsTwoToOne,
        convertsOneToTwo,
        GetItemCount(actor, weapon));

    if (!IsSupportedGripRequest(twoHandedWeapon, oneHandedWeapon, grip)) {
        logger::debug("[D2H EquipDebug] CanEquipWithGrip REJECT unsupported grip request");
        return false;
    }

    // Explicit API requests take precedence over the normal-equip remapping option.
    if (keepsNativeTwoHanded) {
        logger::debug(
            "[D2H EquipDebug] CanEquipWithGrip ACCEPT native 2H request. normal2HAs1H={} is ignored for explicit TwoHanded",
            ModSettings::Settings.playerNormalTwoHandedAsOneHanded);
        return true;
    }

    if (hand == DYNAMIC_TWO_HANDED_API::Hand::kBoth && grip == DYNAMIC_TWO_HANDED_API::Grip::kOneHanded &&
        GetItemCount(actor, weapon) < 2) {
        logger::debug(
            "[D2H EquipDebug] CanEquipWithGrip REJECT dual same-base request: only {} copy/copies",
            GetItemCount(actor, weapon));
        return false;
    }

    // Both-hand 1H grip on a native 2H weapon means two physical copies.
    // A one-hand request also becomes dual 2H if the opposite slot already
    // contains a native 2H weapon (the same base form or a different one).
    // These checks apply through CanEquip AND Equip, including API requests.
    if (convertsTwoToOne) {
        const bool oppositeHas2H =
            (hand == DYNAMIC_TWO_HANDED_API::Hand::kRight && g_leftHandSlot &&
                isTwoHanded(actor->GetEquippedObjectInSlot(g_leftHandSlot))) ||
            (hand == DYNAMIC_TWO_HANDED_API::Hand::kLeft && g_rightHandSlot &&
                isTwoHanded(actor->GetEquippedObjectInSlot(g_rightHandSlot)));
        const bool needsDualPermission = hand == DYNAMIC_TWO_HANDED_API::Hand::kBoth || oppositeHas2H;
        if (needsDualPermission && !CanDualTwoHanded(actor)) {
            logger::debug(
                "[D2H EquipDebug] CanEquipWithGrip REJECT dual native 2H hand={} oppositeHas2H={} dual requirements not met",
                HandName(hand), oppositeHas2H);
            return false;
        }
    }

    if (!actor->IsPlayerRef()) {
        const bool result = convertsTwoToOne && can2h(actor);
        logger::debug("[D2H EquipDebug] CanEquipWithGrip NPC result={}", result);
        return result;
    }

    const auto playerLevel = actor->GetLevel();
    const bool levelOK = MeetsLevel(playerLevel, ModSettings::Settings.playerMinimumLevel);
    if (!levelOK) {
        logger::debug(
            "[D2H EquipDebug] CanEquipWithGrip REJECT player level {}/{}",
            playerLevel,
            ModSettings::Settings.playerMinimumLevel);
        return false;
    }

    if (convertsTwoToOne) {
        const bool enabled = ModSettings::Settings.playerTwoHandedAsOneHanded;
        const bool perkOK = ModSettings::HasRequiredPerk(
            actor, ModSettings::Settings.playerTwoHandedAsOneHandedPerk);
        const bool result = enabled && perkOK;

        logger::debug(
            "[D2H EquipDebug] CanEquipWithGrip 2H->1H enabled={} perkID={:08X} perkOK={} normalEquipRemap={} RESULT={}",
            enabled,
            ModSettings::Settings.playerTwoHandedAsOneHandedPerk,
            perkOK,
            ModSettings::Settings.playerNormalTwoHandedAsOneHanded,
            result);
        return result;
    }

    const bool enabled = ModSettings::Settings.playerOneHandedAsTwoHanded;
    const bool perkOK = ModSettings::HasRequiredPerk(
        actor, ModSettings::Settings.playerOneHandedAsTwoHandedPerk);
    const bool result = enabled && perkOK;

    logger::debug(
        "[D2H EquipDebug] CanEquipWithGrip 1H->2H enabled={} perkID={:08X} perkOK={} RESULT={}",
        enabled,
        ModSettings::Settings.playerOneHandedAsTwoHandedPerk,
        perkOK,
        result);
    return result;
}

bool Hooks::EquipWithGrip(RE::Actor* actor, RE::TESObjectWEAP* weapon, RE::ExtraDataList* extra,
    DYNAMIC_TWO_HANDED_API::Grip grip, DYNAMIC_TWO_HANDED_API::Hand hand) {
    logger::debug(
        "[D2H EquipDebug] EquipWithGrip ENTER actor={} weapon={} grip={} hand={} extra={}",
        actor ? FormLabel(actor) : "<null>",
        weapon ? FormLabel(weapon) : "<null>",
        GripName(grip),
        HandName(hand),
        static_cast<const void*>(extra));
    LogActorSlots(actor, "EquipWithGrip PRE-CHECK");

    const bool canEquip = CanEquipWithGrip(actor, weapon, grip, hand);
    logger::debug("[D2H EquipDebug] EquipWithGrip CanEquipWithGrip={}", canEquip);
    if (!canEquip) {
        logger::debug("[D2H EquipDebug] EquipWithGrip EXIT false (request rejected)");
        return false;
    }

    if (grip == DYNAMIC_TWO_HANDED_API::Grip::kOneHanded && hand == DYNAMIC_TWO_HANDED_API::Hand::kBoth) {
        logger::debug("[D2H EquipDebug] EquipWithGrip dual same-base path: Right then Left");
        const bool right = EquipInSlot(actor, weapon, extra, g_rightHandSlot);
        const bool left = right && EquipInSlot(actor, weapon, extra, g_leftHandSlot);
        LogActorSlots(actor, "EquipWithGrip DUAL AFTER");
        logger::debug(
            "[D2H EquipDebug] EquipWithGrip dual same-base RESULT right={} left={} result={}",
            right,
            left,
            right && left);
        return right && left;
    }

    auto* target = GripSlot(grip, hand);
    logger::debug(
        "[D2H EquipDebug] EquipWithGrip resolved target={} [{:08X}]",
        KnownSlotName(target),
        target ? target->GetFormID() : 0);

    // A two-handed request is explicit and exclusive: if this same base weapon is
    // currently using one-hand slots, clear those copies first so the request can
    // never be redirected/blocked by the normal 2H -> 1H inventory behavior.
    // For one-handed requests we preserve the previous multi-copy behavior, which
    // allows the same base weapon to remain equipped in the opposite hand.
    const bool explicitTwoHanded = grip == DYNAMIC_TWO_HANDED_API::Grip::kTwoHanded;
    const auto itemCount = GetItemCount(actor, weapon);
    logger::debug(
        "[D2H EquipDebug] EquipWithGrip explicitTwoHanded={} itemCount={} cleanupOtherSlots={}",
        explicitTwoHanded,
        itemCount,
        explicitTwoHanded || itemCount <= 1);

    if (explicitTwoHanded || itemCount <= 1) {
        for (auto* slot : std::array{ g_rightHandSlot, g_leftHandSlot, g_twoHandSlot }) {
            if (!slot) {
                logger::debug("[D2H EquipDebug] EquipWithGrip cleanup skipped null slot");
                continue;
            }

            const bool sameAsTarget = slot == target;
            const bool hasSameWeapon = IsEquippedInSlot(actor, weapon, slot);
            logger::debug(
                "[D2H EquipDebug] EquipWithGrip cleanup inspect slot={} [{:08X}] sameAsTarget={} sameWeaponEquipped={}",
                KnownSlotName(slot),
                slot->GetFormID(),
                sameAsTarget,
                hasSameWeapon);

            if (!sameAsTarget && hasSameWeapon) {
                logger::debug(
                    "[D2H EquipDebug] EquipWithGrip cleanup UNEQUIP same weapon from {} before target equip",
                    KnownSlotName(slot));
                UnequipInSlot(actor, weapon, extra, slot);
            }
        }
    }

    const bool result = EquipInSlot(actor, weapon, extra, target);
    LogActorSlots(actor, "EquipWithGrip FINAL");
    logger::debug("[D2H EquipDebug] EquipWithGrip EXIT result={}", result);
    return result;
}

bool Hooks::UnequipWithGrip(RE::Actor* actor, RE::TESObjectWEAP* weapon, RE::ExtraDataList* extra,
    DYNAMIC_TWO_HANDED_API::Grip grip, DYNAMIC_TWO_HANDED_API::Hand hand) {
    logger::debug(
        "[D2H EquipDebug] UnequipWithGrip ENTER actor={} weapon={} grip={} hand={} extra={}",
        actor ? FormLabel(actor) : "<null>",
        weapon ? FormLabel(weapon) : "<null>",
        GripName(grip),
        HandName(hand),
        static_cast<const void*>(extra));
    LogActorSlots(actor, "UnequipWithGrip BEFORE");

    if (!actor || !weapon) {
        logger::debug("[D2H EquipDebug] UnequipWithGrip EXIT false (null actor/weapon)");
        return false;
    }

    if (grip == DYNAMIC_TWO_HANDED_API::Grip::kOneHanded && hand == DYNAMIC_TWO_HANDED_API::Hand::kBoth) {
        const bool right = UnequipInSlot(actor, weapon, extra, g_rightHandSlot);
        const bool left = UnequipInSlot(actor, weapon, extra, g_leftHandSlot);
        LogActorSlots(actor, "UnequipWithGrip DUAL AFTER");
        logger::debug(
            "[D2H EquipDebug] UnequipWithGrip dual result right={} left={} result={}",
            right,
            left,
            right || left);
        return right || left;
    }

    auto* target = GripSlot(grip, hand);
    logger::debug(
        "[D2H EquipDebug] UnequipWithGrip resolved target={} [{:08X}]",
        KnownSlotName(target),
        target ? target->GetFormID() : 0);

    // Partial-hand API semantics while the weapon is physically in TwoHand:
    //   Unequip Right -> release the right hand and keep the weapon in Left.
    //   Unequip Left  -> release the left hand and keep the weapon in Right.
    //
    // This is a state conversion, not a rejected/mismatched unequip. We first remove
    // the real TwoHand instance and then equip that same base weapon in the opposite
    // one-hand slot. The explicit slots remain the source of truth throughout, so we
    // never call UnequipObject(Right/Left) for an item that is actually in TwoHand.
    const bool partialOneHandRequest =
        grip == DYNAMIC_TWO_HANDED_API::Grip::kOneHanded &&
        (hand == DYNAMIC_TWO_HANDED_API::Hand::kRight || hand == DYNAMIC_TWO_HANDED_API::Hand::kLeft);

    if (partialOneHandRequest &&
        !IsEquippedInSlot(actor, weapon, target) &&
        IsEquippedInSlot(actor, weapon, g_twoHandSlot)) {
        auto* remainingSlot = hand == DYNAMIC_TWO_HANDED_API::Hand::kRight
            ? g_leftHandSlot
            : g_rightHandSlot;
        const auto remainingHand = hand == DYNAMIC_TWO_HANDED_API::Hand::kRight
            ? DYNAMIC_TWO_HANDED_API::Hand::kLeft
            : DYNAMIC_TWO_HANDED_API::Hand::kRight;

        logger::debug(
            "[D2H EquipDebug] UnequipWithGrip TwoHand partial conversion requestedHand={} remainingHand={} remainingSlot={}",
            HandName(hand),
            HandName(remainingHand),
            KnownSlotName(remainingSlot));

        // A native 2H weapon may only be left in a one-hand slot if the actor meets
        // the normal 2H -> 1H requirements. A native 1H weapon returning from a
        // temporary 1H -> 2H state needs no extra permission to become 1H again.
        if (isTwoHanded(weapon) &&
            !CanEquipWithGrip(actor, weapon, DYNAMIC_TWO_HANDED_API::Grip::kOneHanded, remainingHand)) {
            logger::debug(
                "[D2H EquipDebug] UnequipWithGrip TwoHand partial conversion REJECTED: cannot keep native 2H weapon in {}",
                HandName(remainingHand));
            return false;
        }

        const bool removedTwoHand = UnequipInSlot(actor, weapon, extra, g_twoHandSlot);
        logger::debug(
            "[D2H EquipDebug] UnequipWithGrip TwoHand partial conversion removedTwoHand={}",
            removedTwoHand);
        if (!removedTwoHand) {
            return false;
        }

        const bool equipRequested = EquipInSlot(actor, weapon, nullptr, remainingSlot);
        const bool finalRemaining = IsEquippedInSlot(actor, weapon, remainingSlot);
        const bool finalTwoHand = IsEquippedInSlot(actor, weapon, g_twoHandSlot);
        const bool converted = equipRequested && finalRemaining && !finalTwoHand;

        logger::debug(
            "[D2H EquipDebug] UnequipWithGrip TwoHand partial conversion result={} equipRequested={} finalRemaining={} finalTwoHand={}",
            converted,
            equipRequested,
            finalRemaining,
            finalTwoHand);
        LogActorSlots(actor, "UnequipWithGrip TwoHand partial conversion FINAL");

        // Do not leave the actor unarmed if Skyrim unexpectedly rejected the one-hand
        // equip. Restore the original TwoHand state and report failure to the caller.
        if (!converted) {
            if (!finalTwoHand) {
                logger::debug(
                    "[D2H EquipDebug] UnequipWithGrip TwoHand partial conversion failed; restoring TwoHand");
                EquipInSlot(actor, weapon, nullptr, g_twoHandSlot);
            }
            return false;
        }

        return true;
    }

    // If the requested physical hand does not contain the weapon and the weapon is
    // not in TwoHand either, there is nothing for this partial-hand request to do.
    if (partialOneHandRequest && !IsEquippedInSlot(actor, weapon, target)) {
        logger::debug(
            "[D2H EquipDebug] UnequipWithGrip partial-hand NO-OP: slot {} does not contain weapon and TwoHand is empty. legacyHandView={}",
            KnownSlotName(target),
            HasLegacyHandView(actor, weapon, target));
        LogActorSlots(actor, "UnequipWithGrip partial-hand NO-OP");
        return false;
    }

    RE::BGSEquipSlot* opposite = nullptr;
    bool preserveOpposite = false;
    if (grip == DYNAMIC_TWO_HANDED_API::Grip::kOneHanded) {
        if (hand == DYNAMIC_TWO_HANDED_API::Hand::kRight) opposite = g_leftHandSlot;
        if (hand == DYNAMIC_TWO_HANDED_API::Hand::kLeft) opposite = g_rightHandSlot;
        preserveOpposite = opposite && IsEquippedInRequestedHand(actor, weapon, opposite);
    }

    logger::debug(
        "[D2H EquipDebug] UnequipWithGrip partial-hand target={} opposite={} preserveOpposite={}",
        KnownSlotName(target),
        KnownSlotName(opposite),
        preserveOpposite);

    const bool result = UnequipInSlot(actor, weapon, extra, target);

    // A targeted Right/Left API unequip must never consume the other physical copy.
    // If Skyrim collapsed both hands while processing the removal, restore only the
    // previously occupied opposite hand using a freshly resolved inventory instance.
    if (result && preserveOpposite && opposite && !IsEquippedInRequestedHand(actor, weapon, opposite)) {
        logger::debug(
            "[D2H EquipDebug] UnequipWithGrip opposite hand was lost; restoring {} with fresh extra resolution",
            KnownSlotName(opposite));
        const bool restored = EquipInSlot(actor, weapon, nullptr, opposite);
        logger::debug(
            "[D2H EquipDebug] UnequipWithGrip opposite restore result={}",
            restored);
    }

    LogActorSlots(actor, "UnequipWithGrip FINAL");
    logger::debug("[D2H EquipDebug] UnequipWithGrip EXIT result={}", result);
    return result;
}

bool HasOffhandItem(RE::Actor* actor, RE::TESForm* itemBeingEquipped) {
    if (!actor) return false;

    auto inventory = actor->GetInventory();
    for (const auto& [item, data] : inventory) {
        int count = data.first;
        if (count <= 0) continue;

        // Se o item no invent�rio for o mesmo que estamos tentando equipar,
        // precisamos ter pelo menos 2 dele para sobrar um para a outra m�o.
        if (item == itemBeingEquipped) {
            count--;
        }

        if (count > 0) {
            if (Hooks::isTwoHanded(item) || Hooks::isOneHanded(item) || isShield(item)) {
				logger::debug("  - Item dispon�vel para a m�o esquerda encontrado: '{}' (ID: {})", item->GetName(), item->GetFormID());
                return true;
            }
        }
    }
    return false;
}

void CheckForNPC(RE::Actor* npc) {
    if (!npc || npc->IsPlayerRef() || !can2h(npc)) {
        logger::debug("--- [CheckAndEquipHandle] NPC inv�lido : '{}'", npc->GetName());
            return;
    }

    auto equipManager = RE::ActorEquipManager::GetSingleton();
    if (!equipManager) {
        return;
    }
	logger::debug("--- [CheckAndEquipHandle] In�cio da verifica��o para NPC: '{}' (ID: {}) ---", npc->GetName(), npc->GetFormID());
    // 1. Obter o status atual do equipamento
    auto equippedItemL = npc->GetEquippedObjectInSlot(Hooks::g_leftHandSlot);
    auto equippedItemR = npc->GetEquippedObjectInSlot(Hooks::g_rightHandSlot);
    auto equippedItem2H = npc->GetEquippedObjectInSlot(Hooks::g_twoHandSlot);

    bool isR_2H = Hooks::isTwoHanded(equippedItemR);
    bool isL_2H = Hooks::isTwoHanded(equippedItemL);
    bool is2H_2H = Hooks::isTwoHanded(equippedItem2H);

    // 2. Verifica��es de sa�da antecipada
    if (isL_2H && isR_2H) {
        //logger::info("  - Status: NPC j� est� empunhando duas armas de duas m�os (Dual 2H).");
        //logger::info("--- [CheckAndEquipHandle] Fim da verifica��o ---");
        return;  // J� est� em dual wielding 2H
    }

    if (isR_2H && equippedItemL != nullptr) {
        std::string leftItemName = "Item Desconhecido";
        if (equippedItemL) {
            leftItemName = equippedItemL->GetName();
        }
        //logger::info("  - Status: NPC j� est� empunhando 2H na direita e '{}' na esquerda. N�o interferir.",leftItemName);
        //logger::info("--- [CheckAndEquipHandle] Fim da verifica��o ---");
        return;
    }

    // 4. Escanear invent�rio para itens N�O EQUIPADOS
    /*logger::info("  - Escaneando invent�rio...");*/
    std::vector<RE::TESObjectWEAP*> available2H;
    std::vector<RE::TESObjectWEAP*> available1H;
    std::vector<RE::TESObjectARMO*> availableShields;
    std::vector<RE::SpellItem*> spells;

    RE::TESObjectWEAP* r_weap = equippedItemR ? equippedItemR->As<RE::TESObjectWEAP>() : nullptr;
    RE::TESObjectWEAP* l_weap = equippedItemL ? equippedItemL->As<RE::TESObjectWEAP>() : nullptr;
    RE::TESObjectARMO* l_armo = equippedItemL ? equippedItemL->As<RE::TESObjectARMO>() : nullptr;

    auto inventory = npc->GetInventory();
    for (const auto& [item, data] : inventory) {
        int count = data.first;
        if (count <= 0) continue;
        if (item == r_weap) count--;
        if (item == l_weap) count--;
        if (item == equippedItem2H) count--;
        if (item == l_armo) count--;
        for (int i = 0; i < count; i++) {
            if (Hooks::isTwoHanded(item)) {
                available2H.push_back(item->As<RE::TESObjectWEAP>());
            }
            else if (Hooks::isOneHanded(item)) {
                available1H.push_back(item->As<RE::TESObjectWEAP>());
            }
            else if (isShield(item)) {
                availableShields.push_back(item->As<RE::TESObjectARMO>());
            }
        }
    }

    int total2HCount = (int)available2H.size() + (isR_2H ? 1 : 0) + (isL_2H ? 1 : 0) + (is2H_2H ? 1 : 0);
	logger::debug("  - NPC possui {} armas de 2 m�os dispon�veis no invent�rio (incluindo as equipadas).", total2HCount);
    // Se ele s� tem uma 2H E n�o tem absolutamente nada para a m�o esquerda (1H, Escudo ou algo j� equipado)
    bool nothingForLeftHand = available1H.empty() && availableShields.empty();
	logger::debug("    - Itens dispon�veis para a m�o esquerda: 1H = {}, Escudos = {}.", available1H.size(), availableShields.size());
    if (total2HCount <= 1 && nothingForLeftHand) {
        logger::debug("  - Status: NPC s� possui uma 2H e nada para a m�o esquerda. Mantendo equipamento original.");
        return;
    }

    RE::TESObjectWEAP* weaponForRight = nullptr;

    // PASSO 1: Garantir Arma de 2 M�os na Direita
    if (Hooks::isTwoHanded(equippedItemR)) {
        weaponForRight = equippedItemR->As<RE::TESObjectWEAP>();
    }
    else if (Hooks::isTwoHanded(equippedItem2H)) {
        // Tem uma 2H no slot vanilla: MOVE para a m�o direita
        weaponForRight = equippedItem2H->As<RE::TESObjectWEAP>();
        equipManager->UnequipObject(npc, weaponForRight, nullptr, 1, Hooks::g_twoHandSlot, false, true, true);
        Hooks::EquipWithGrip(npc, weaponForRight, nullptr,
            DYNAMIC_TWO_HANDED_API::Grip::kOneHanded, DYNAMIC_TWO_HANDED_API::Hand::kRight);
    }
    else if (!available2H.empty()) {
        weaponForRight = available2H[0];
        available2H.erase(available2H.begin()); // Removemos pois ser� usada
        Hooks::EquipWithGrip(npc, weaponForRight, nullptr,
            DYNAMIC_TWO_HANDED_API::Grip::kOneHanded, DYNAMIC_TWO_HANDED_API::Hand::kRight);
    }

    // Se n�o conseguimos uma 2H para a direita, paramos a l�gica de "Dual 2H" aqui
    if (!weaponForRight) return;

    // PASSO 2: Tentar Arma de 2 M�os na Esquerda
    bool isLeftAlready2H = Hooks::isTwoHanded(equippedItemL);

    if (!isLeftAlready2H) {
        if (!available2H.empty()) {
            // Prioridade: Equipar segunda arma de 2 m�os
            Hooks::EquipWithGrip(npc, available2H[0], nullptr,
                DYNAMIC_TWO_HANDED_API::Grip::kOneHanded, DYNAMIC_TWO_HANDED_API::Hand::kLeft);
        }
        else if (equippedItemL == nullptr) {
            // Fallback: Apenas se a m�o esquerda estiver vazia
            if (!available1H.empty()) {
                EquipItemWithGripChange(npc, available1H[0], Hooks::g_leftHandSlot);
            }
            else if (!availableShields.empty()) {
                EquipItemWithGripChange(npc, availableShields[0], Hooks::g_shield);
            }
        }
    }
}

RE::BSEventNotifyControl Hooks::NpcCombatTracker::ProcessEvent(const RE::TESCombatEvent* a_event, RE::BSTEventSource<RE::TESCombatEvent>*)
{
    if (!a_event || !a_event->actor) {
        return RE::BSEventNotifyControl::kContinue;
    }

    auto actor = a_event->actor.get();
    auto* npc = actor->As<RE::Actor>();

    if (npc) {
        switch (a_event->newState.get()) {
        case RE::ACTOR_COMBAT_STATE::kCombat:
            CheckForNPC(npc);
            break;
        case RE::ACTOR_COMBAT_STATE::kNone:
            CheckForNPC(npc);
            break;
        }
    }

    return RE::BSEventNotifyControl::kContinue;
}

void Hooks::RegisterSinksForExistingCombatants() {
    SKSE::log::info("[NpcCombatTracker] Verificando NPCs j� em combate ap�s carregar o jogo...");

    auto* processLists = RE::ProcessLists::GetSingleton();
    if (!processLists) {
        SKSE::log::warn("[NpcCombatTracker] N�o foi poss�vel obter ProcessLists.");
        return;
    }

    // Itera sobre todos os atores que est�o "ativos" no jogo
    for (auto& actorHandle : processLists->highActorHandles) {
        if (auto actor = actorHandle.get().get()) {
            // A fun��o IsInCombat() nos diz se o ator j� est� em um estado de combate
            if (!actor->IsPlayerRef()) {
                CheckForNPC(actor);
            }

        }
    }
    SKSE::log::info("[NpcCombatTracker] Verifica��o conclu�da.");
}

void Hooks::Install()
{
    auto& trampoline = SKSE::GetTrampoline();
    constexpr size_t size_per_hook = 14;
    constexpr size_t NUM_TRAMPOLINE_HOOKS = 2;
    trampoline.create(size_per_hook * NUM_TRAMPOLINE_HOOKS);

    const REL::Relocation<std::uintptr_t> target{ REL::RelocationID(37938, 38894) };
    Equip2H::func =
        trampoline.write_call<5>(target.address() + REL::Relocate(0xe5, 0x170), Equip2H::thunk);
    const REL::Relocation<std::uintptr_t> targetU{ REL::RelocationID(37945, 38901) };
    Unequip2H::func =
        trampoline.write_call<5>(targetU.address() + REL::Relocate(0x138, 0x1b9), Unequip2H::thunk);
}

void Hooks::Equip2H::thunk(std::int64_t* a, RE::Actor* a_actor, RE::TESForm* a_form, std::int64_t* extraData,
    int count, std::int64_t* equipSlot, char queueEquip, char forceEquip,
    char playSounds, char applyNow) {
    logger::debug(
        "[D2H EquipDebug] Equip2H::thunk ENTER actor={} form={} count={} incomingSlot={} ptr={} "
        "queueEquip={} forceEquip={} playSounds={} applyNow={} forcedGripOperation={}",
        a_actor ? FormLabel(a_actor) : "<null>",
        a_form ? FormLabel(a_form) : "<null>",
        count,
        RawSlotName(equipSlot),
        static_cast<const void*>(equipSlot),
        static_cast<int>(queueEquip),
        static_cast<int>(forceEquip),
        static_cast<int>(playSounds),
        static_cast<int>(applyNow),
        forcedGripOperation);

    if (a_actor) {
        LogActorSlots(a_actor, "Equip2H::thunk ENTER");
    }

    if (forcedGripOperation) {
        logger::debug(
            "[D2H EquipDebug] Equip2H::thunk BYPASS because forcedGripOperation=true; forwarding slot={}",
            RawSlotName(equipSlot));
        func(a, a_actor, a_form, extraData, count, equipSlot, queueEquip, forceEquip, playSounds, applyNow);
        if (a_actor) {
            LogActorSlots(a_actor, "Equip2H::thunk forcedGripOperation AFTER ORIGINAL");
        }
        logger::debug("[D2H EquipDebug] Equip2H::thunk EXIT forcedGripOperation path");
        return;
    }

    if (!a_actor) {
        logger::debug("[D2H EquipDebug] Equip2H::thunk actor=null; forwarding untouched");
        func(a, a_actor, a_form, extraData, count, equipSlot, queueEquip, forceEquip, playSounds, applyNow);
        logger::debug("[D2H EquipDebug] Equip2H::thunk EXIT null actor path");
        return;
    }

    if (a_actor->IsPlayerRef()) {
        auto* weapon = a_form ? a_form->As<RE::TESObjectWEAP>() : nullptr;
        const bool is2H = weapon && Hooks::isTwoHanded(weapon);
        const bool is1H = weapon && Hooks::isOneHanded(weapon);
        const bool normalRemapEnabled = ModSettings::Settings.playerNormalTwoHandedAsOneHanded;

        logger::debug(
            "[D2H EquipDebug] PLAYER normal-equip evaluation weapon={} is2H={} is1H={} "
            "Enable2HAs1H={} Normal2HUses1HSlot={} RequiredPerk={:08X} PlayerLevel={} RequiredLevel={}",
            weapon ? FormLabel(weapon) : "<not-a-weapon>",
            is2H,
            is1H,
            ModSettings::Settings.playerTwoHandedAsOneHanded,
            normalRemapEnabled,
            ModSettings::Settings.playerTwoHandedAsOneHandedPerk,
            a_actor->GetLevel(),
            ModSettings::Settings.playerMinimumLevel);

        // Only NORMAL inventory equip is remapped. Explicit Dynamic2H API calls
        // set forcedGripOperation, so "Equip as Two-handed" always uses g_twoHandSlot.
        bool canRemapToOneHand = false;
        if (weapon && is2H && normalRemapEnabled) {
            canRemapToOneHand = CanEquipWithGrip(
                a_actor,
                weapon,
                DYNAMIC_TWO_HANDED_API::Grip::kOneHanded,
                DYNAMIC_TWO_HANDED_API::Hand::kRight);
        }

        logger::debug(
            "[D2H EquipDebug] PLAYER normal-equip decision weaponPresent={} is2H={} normalRemapEnabled={} canRemapToOneHand={}",
            static_cast<bool>(weapon),
            is2H,
            normalRemapEnabled,
            canRemapToOneHand);

        // Second-equip -> two-handed rule.
        //
        // If a SINGLE copy of the same weapon is already equipped in exactly one
        // one-hand slot and the player tries to equip it into the other hand,
        // interpret that second equip as a request to hold the weapon with both
        // hands instead of trying to duplicate the same physical item.
        //
        // This applies to:
        //   1) native 1H weapons that satisfy the 1H -> 2H requirements;
        //   2) native 2H weapons currently using a one-hand slot because
        //      "Normal 2H equip uses a one-hand slot" is enabled.
        //
        // With 2+ copies of the same base weapon we deliberately do NOT enter
        // this path, preserving vanilla-style dual wield (Right + Left).
        const bool normal1HSecondEquipAs2H =
            ModSettings::Settings.playerNormalOneHandedSecondEquipAsTwoHanded;

        if (weapon && normal1HSecondEquipAs2H && (is1H || (is2H && normalRemapEnabled))) {
            auto* rightObject = g_rightHandSlot ? a_actor->GetEquippedObjectInSlot(g_rightHandSlot) : nullptr;
            auto* leftObject = g_leftHandSlot ? a_actor->GetEquippedObjectInSlot(g_leftHandSlot) : nullptr;

            const bool sameInRight = rightObject == weapon;
            const bool sameInLeft = leftObject == weapon;
            const bool sameInExactlyOneHand = sameInRight != sameInLeft;
            const auto itemCount = GetItemCount(a_actor, weapon);
            const bool hasMultipleCopies = itemCount >= 2;

            RE::BGSEquipSlot* currentOneHandSlot = nullptr;
            if (sameInRight) {
                currentOneHandSlot = g_rightHandSlot;
            } else if (sameInLeft) {
                currentOneHandSlot = g_leftHandSlot;
            }

            RE::BGSEquipSlot* requestedSlot = nullptr;
            if (equipSlot == reinterpret_cast<std::int64_t*>(g_rightHandSlot)) {
                requestedSlot = g_rightHandSlot;
            } else if (equipSlot == reinterpret_cast<std::int64_t*>(g_leftHandSlot)) {
                requestedSlot = g_leftHandSlot;
            } else if (equipSlot == reinterpret_cast<std::int64_t*>(g_twoHandSlot)) {
                requestedSlot = g_twoHandSlot;
            } else if (sameInExactlyOneHand) {
                // Skyrim frequently supplies EitherHand/null/an internal pointer here
                // rather than an explicit Left/Right slot. In that case a second
                // equip of a single copy is treated as the opposite-hand request.
                requestedSlot = sameInRight ? g_leftHandSlot : g_rightHandSlot;
            }

            const bool requestsOtherHand =
                sameInExactlyOneHand && requestedSlot && requestedSlot != currentOneHandSlot;

            bool canUseTwoHands = false;
            if (requestsOtherHand && !hasMultipleCopies) {
                if (is1H) {
                    canUseTwoHands = CanEquipWithGrip(
                        a_actor,
                        weapon,
                        DYNAMIC_TWO_HANDED_API::Grip::kTwoHanded,
                        DYNAMIC_TWO_HANDED_API::Hand::kBoth);
                } else if (is2H && normalRemapEnabled) {
                    // Returning a native 2H weapon to its native two-hand grip is
                    // always supported. The shared normal-second-equip checkbox
                    // controls whether this automatic conversion happens.
                    canUseTwoHands = true;
                }
            }

            logger::debug(
                "[D2H EquipDebug] PLAYER second-equip->2H evaluation weapon={} count={} multipleCopies={} "
                "sameRight={} sameLeft={} currentSlot={} requestedSlot={} incomingSlot={} "
                "is1H={} is2H={} normal2HUses1HSlot={} normal1HSecondEquipAs2H={} "
                "requestsOtherHand={} canUseTwoHands={}",
                FormLabel(weapon),
                itemCount,
                hasMultipleCopies,
                sameInRight,
                sameInLeft,
                KnownSlotName(currentOneHandSlot),
                KnownSlotName(requestedSlot),
                RawSlotName(equipSlot),
                is1H,
                is2H,
                normalRemapEnabled,
                normal1HSecondEquipAs2H,
                requestsOtherHand,
                canUseTwoHands);

            if (requestsOtherHand && !hasMultipleCopies && canUseTwoHands && g_twoHandSlot) {
                logger::debug(
                    "[D2H EquipDebug] PLAYER second-equip->2H TRIGGER: unequip {} then equip TwoHand",
                    KnownSlotName(currentOneHandSlot));

                // Remove the existing one-hand instance first. Because this path is
                // restricted to a single copy, there is no ambiguity about which
                // inventory item is being moved. forcedGripOperation inside
                // UnequipInSlot prevents our unequip hook from rewriting the request.
                UnequipInSlot(a_actor, weapon, nullptr, currentOneHandSlot);
                LogActorSlots(a_actor, "PLAYER second-equip->2H AFTER ONE-HAND UNEQUIP");

                auto* originalSlot = weapon->GetEquipSlot();
                weapon->SetEquipSlot(g_twoHandSlot);
                auto* targetEquipSlot = reinterpret_cast<std::int64_t*>(g_twoHandSlot);

                logger::debug(
                    "[D2H EquipDebug] PLAYER second-equip->2H CALL ORIGINAL target=TwoHand [{:08X}] "
                    "originalWeaponSlot={} [{:08X}] argPtr={}",
                    g_twoHandSlot->GetFormID(),
                    KnownSlotName(originalSlot),
                    originalSlot ? originalSlot->GetFormID() : 0,
                    static_cast<const void*>(targetEquipSlot));

                {
                    ScopedNormalPlayerEquip normalEquipScope;
                    func(a, a_actor, a_form, extraData, count, targetEquipSlot,
                        false, true, playSounds, true);
                }

                LogActorSlots(a_actor, "PLAYER second-equip->2H AFTER ORIGINAL");
                weapon->SetEquipSlot(originalSlot);

                logger::debug(
                    "[D2H EquipDebug] PLAYER second-equip->2H EXIT restored weapon slot={} [{:08X}]",
                    KnownSlotName(originalSlot),
                    originalSlot ? originalSlot->GetFormID() : 0);
                return;
            }
        }

        // Without the optional second-equip setting, never remap a second
        // equip of the *only* copy into the empty hand. Let the original Skyrim
        // toggle handle it; this avoids trying to dual-equip one physical item.
        const bool singleAlreadyInOneHand = weapon && is2H &&
            GetItemCount(a_actor, weapon) == 1 &&
            ((g_rightHandSlot && a_actor->GetEquippedObjectInSlot(g_rightHandSlot) == weapon) ||
             (g_leftHandSlot && a_actor->GetEquippedObjectInSlot(g_leftHandSlot) == weapon));
        if (singleAlreadyInOneHand && !normal1HSecondEquipAs2H) {
            logger::debug(
                "[D2H EquipDebug] PLAYER second normal equip: option OFF, forwarding vanilla toggle without one-hand remap");
            ScopedNormalPlayerEquip normalEquipScope;
            func(a, a_actor, a_form, extraData, count, equipSlot,
                queueEquip, forceEquip, playSounds, applyNow);
            LogActorSlots(a_actor, "PLAYER second normal equip vanilla result");
            return;
        }

        if (weapon && is2H && normalRemapEnabled && canRemapToOneHand) {
            // Automatic vanilla-like selection for NORMAL equip:
            // Right empty -> Right. Right occupied -> Left.
            // This also lets a second copy of the same base weapon progress to dual wield.
            auto* rightObject = g_rightHandSlot ? a_actor->GetEquippedObjectInSlot(g_rightHandSlot) : nullptr;
            auto* leftObject = g_leftHandSlot ? a_actor->GetEquippedObjectInSlot(g_leftHandSlot) : nullptr;
            auto* twoHandObject = g_twoHandSlot ? a_actor->GetEquippedObjectInSlot(g_twoHandSlot) : nullptr;
            auto* targetSlot = rightObject ? g_leftHandSlot : g_rightHandSlot;
            const auto targetHand = targetSlot == g_leftHandSlot
                ? DYNAMIC_TWO_HANDED_API::Hand::kLeft
                : DYNAMIC_TWO_HANDED_API::Hand::kRight;
            const bool targetAllowed = targetSlot && CanEquipWithGrip(
                a_actor, weapon, DYNAMIC_TWO_HANDED_API::Grip::kOneHanded, targetHand);
            if (!targetAllowed) {
                logger::debug(
                    "[D2H EquipDebug] PLAYER auto-slot REMAP DENIED target={} (dual 2H or conversion requirements); forwarding vanilla equip",
                    KnownSlotName(targetSlot));
            }

            logger::debug(
                "[D2H EquipDebug] PLAYER auto-slot: Right={} Left={} TwoHand={} -> target={} [{:08X}]",
                FormLabel(rightObject),
                FormLabel(leftObject),
                FormLabel(twoHandObject),
                KnownSlotName(targetSlot),
                targetSlot ? targetSlot->GetFormID() : 0);

            if (targetAllowed) {
                auto* originalSlot = weapon->GetEquipSlot();
                logger::debug(
                    "[D2H EquipDebug] PLAYER REMAP BEFORE SetEquipSlot weaponSlot={} [{:08X}] incomingArgSlot={} ptr={}",
                    KnownSlotName(originalSlot),
                    originalSlot ? originalSlot->GetFormID() : 0,
                    RawSlotName(equipSlot),
                    static_cast<const void*>(equipSlot));

                weapon->SetEquipSlot(targetSlot);

                // The hooked function receives its own equip-slot argument. Passing the
                // original TwoHand slot here prevented the checkbox from reliably working.
                auto* targetEquipSlot = reinterpret_cast<std::int64_t*>(targetSlot);

                logger::debug(
                    "[D2H EquipDebug] PLAYER REMAP CALL ORIGINAL targetSlot={} [{:08X}] argPtr={} "
                    "queue=false force=true applyNow=true weaponCurrentSlot={} [{:08X}]",
                    KnownSlotName(targetSlot),
                    targetSlot->GetFormID(),
                    static_cast<const void*>(targetEquipSlot),
                    KnownSlotName(weapon->GetEquipSlot()),
                    weapon->GetEquipSlot() ? weapon->GetEquipSlot()->GetFormID() : 0);

                {
                    ScopedNormalPlayerEquip normalEquipScope;
                    func(a, a_actor, a_form, extraData, count, targetEquipSlot,
                        false, true, playSounds, true);
                }

                logger::debug("[D2H EquipDebug] PLAYER REMAP RETURN ORIGINAL");
                LogActorSlots(a_actor, "Equip2H::thunk PLAYER REMAP AFTER ORIGINAL");

                weapon->SetEquipSlot(originalSlot);
                logger::debug(
                    "[D2H EquipDebug] PLAYER REMAP restored weapon slot={} [{:08X}] and EXIT",
                    KnownSlotName(originalSlot),
                    originalSlot ? originalSlot->GetFormID() : 0);
                return;
            }

            logger::debug("[D2H EquipDebug] PLAYER REMAP could not resolve target slot; falling back to original call");
        }

        logger::debug(
            "[D2H EquipDebug] PLAYER FORWARD ORIGINAL unchanged slot={} ptr={}",
            RawSlotName(equipSlot),
            static_cast<const void*>(equipSlot));
        {
            ScopedNormalPlayerEquip normalEquipScope;
            func(a, a_actor, a_form, extraData, count, equipSlot, queueEquip, forceEquip, playSounds, applyNow);
        }
        LogActorSlots(a_actor, "Equip2H::thunk PLAYER ORIGINAL AFTER");
        logger::debug("[D2H EquipDebug] Equip2H::thunk EXIT player original path");
        return;
    }

    if (Hooks::isTwoHanded(a_form)) {
        const bool npcCan2H = can2h(a_actor);
        const bool hasOffhand = HasOffhandItem(a_actor, a_form);
        logger::debug(
            "[D2H EquipDebug] NPC 2H evaluation actor={} can2h={} hasOffhandItem={}",
            FormLabel(a_actor),
            npcCan2H,
            hasOffhand);

        // Usa a funcao can2h que ja criamos com os requisitos
        if (npcCan2H && hasOffhand) {
            auto* weapon = a_form->As<RE::TESObjectWEAP>();
            auto* originalSlot = weapon->GetEquipSlot();

            logger::debug(
                "[D2H EquipDebug] NPC REMAP weapon={} originalWeaponSlot={} incomingArgSlot={} -> weapon slot RightHand",
                FormLabel(weapon),
                KnownSlotName(originalSlot),
                RawSlotName(equipSlot));

            weapon->SetEquipSlot(Hooks::g_rightHandSlot);
            func(a, a_actor, a_form, extraData, count, equipSlot, false, true, playSounds, true);
            LogActorSlots(a_actor, "Equip2H::thunk NPC REMAP AFTER ORIGINAL");
            weapon->SetEquipSlot(originalSlot);

            logger::debug("[D2H EquipDebug] Equip2H::thunk EXIT NPC remap path");
            return;
        }
    }

    logger::debug(
        "[D2H EquipDebug] Equip2H::thunk FALLBACK ORIGINAL actor={} form={} slot={}",
        FormLabel(a_actor),
        a_form ? FormLabel(a_form) : "<null>",
        RawSlotName(equipSlot));
    func(a, a_actor, a_form, extraData, count, equipSlot, queueEquip, forceEquip, playSounds, applyNow);
    LogActorSlots(a_actor, "Equip2H::thunk FALLBACK AFTER");
    logger::debug("[D2H EquipDebug] Equip2H::thunk EXIT fallback path");
}

std::int64_t Hooks::Unequip2H::thunk(std::int64_t* a, RE::Actor* a_actor, RE::TESForm* a_form,
    std::int64_t* extraData) {
    logger::debug(
        "[D2H EquipDebug] Unequip2H::thunk ENTER actor={} form={} forcedGripOperation={} extra={}",
        a_actor ? FormLabel(a_actor) : "<null>",
        a_form ? FormLabel(a_form) : "<null>",
        forcedGripOperation,
        static_cast<const void*>(extraData));

    if (a_actor) {
        LogActorSlots(a_actor, "Unequip2H::thunk ENTER");
    }

    if (forcedGripOperation) {
        logger::debug("[D2H EquipDebug] Unequip2H::thunk BYPASS forcedGripOperation=true");
        const auto result = func(a, a_actor, a_form, extraData);
        if (a_actor) {
            LogActorSlots(a_actor, "Unequip2H::thunk forced AFTER ORIGINAL");
        }
        logger::debug("[D2H EquipDebug] Unequip2H::thunk EXIT forced path result={}", result);
        return result;
    }

    if (!a_actor) {
        logger::debug("[D2H EquipDebug] Unequip2H::thunk actor=null; forwarding");
        const auto result = func(a, a_actor, a_form, extraData);
        logger::debug("[D2H EquipDebug] Unequip2H::thunk EXIT null actor result={}", result);
        return result;
    }

    // Skyrim's normal Inventory/Favorites behavior often turns a second click on
    // an already equipped 1H weapon directly into UnequipObject. In that case the
    // Equip2H hook is never reached. When the optional setting is enabled, remember
    // that direct vanilla toggle and, after Skyrim performs its normal unequip, move
    // the same single-copy weapon into the TwoHand slot.
    //
    // This block intentionally ignores:
    //   * Dynamic2H API operations (forcedGripOperation returned above);
    //   * nested unequips caused by equipping some OTHER item;
    //   * multiple copies of the same base weapon, preserving normal dual-wield logic.
    RE::TESObjectWEAP* normalToggleToTwoHandWeapon = nullptr;
    bool convertNormalToggleToTwoHand = false;

    if (a_actor->IsPlayerRef() &&
        !normalPlayerEquipInProgress &&
        ModSettings::Settings.playerNormalOneHandedSecondEquipAsTwoHanded &&
        (Hooks::isOneHanded(a_form) ||
            (Hooks::isTwoHanded(a_form) &&
                ModSettings::Settings.playerNormalTwoHandedAsOneHanded))) {
        auto* candidate = a_form->As<RE::TESObjectWEAP>();
        auto* rightObject = Hooks::g_rightHandSlot ?
            a_actor->GetEquippedObjectInSlot(Hooks::g_rightHandSlot) : nullptr;
        auto* leftObject = Hooks::g_leftHandSlot ?
            a_actor->GetEquippedObjectInSlot(Hooks::g_leftHandSlot) : nullptr;
        auto* twoHandObject = Hooks::g_twoHandSlot ?
            a_actor->GetEquippedObjectInSlot(Hooks::g_twoHandSlot) : nullptr;

        const bool sameRight = rightObject == candidate;
        const bool sameLeft = leftObject == candidate;
        const bool exactlyOneHand = sameRight != sameLeft;
        const auto itemCount = GetItemCount(a_actor, candidate);
        const bool singleCopy = itemCount == 1;
        const bool alreadyTwoHanded = twoHandObject == candidate;
        const bool eligibleNativeTwoHand = candidate && Hooks::isTwoHanded(candidate);
        const auto currentHand = sameLeft
            ? DYNAMIC_TWO_HANDED_API::Hand::kLeft
            : DYNAMIC_TWO_HANDED_API::Hand::kRight;
        const bool validOneHandGrip = !eligibleNativeTwoHand || CanEquipWithGrip(
            a_actor, candidate, DYNAMIC_TWO_HANDED_API::Grip::kOneHanded, currentHand);
        const bool canUseTwoHands = candidate && exactlyOneHand && singleCopy && !alreadyTwoHanded &&
            validOneHandGrip && CanEquipWithGrip(
                a_actor,
                candidate,
                DYNAMIC_TWO_HANDED_API::Grip::kTwoHanded,
                DYNAMIC_TWO_HANDED_API::Hand::kBoth);

        logger::debug(
            "[D2H EquipDebug] Unequip2H normal-toggle->2H evaluation weapon={} setting={} "
            "normalEquipInProgress={} count={} sameRight={} sameLeft={} exactlyOneHand={} "
            "alreadyTwoHanded={} native2H={} validOneHandGrip={} canUseTwoHands={}",
            candidate ? FormLabel(candidate) : "<null>",
            ModSettings::Settings.playerNormalOneHandedSecondEquipAsTwoHanded,
            normalPlayerEquipInProgress,
            itemCount,
            sameRight,
            sameLeft,
            exactlyOneHand,
            alreadyTwoHanded,
            eligibleNativeTwoHand,
            validOneHandGrip,
            canUseTwoHands);

        if (canUseTwoHands) {
            normalToggleToTwoHandWeapon = candidate;
            convertNormalToggleToTwoHand = true;
        }
    }

    RE::TESObjectWEAP* weapon = nullptr;
    RE::BGSEquipSlot* originalSlot = nullptr;

    if (Hooks::isTwoHanded(a_form)) {
        weapon = a_form->As<RE::TESObjectWEAP>();
        originalSlot = weapon->GetEquipSlot();
        auto* leftItem = a_actor->GetEquippedObjectInSlot(Hooks::g_leftHandSlot);
        auto* rightItem = a_actor->GetEquippedObjectInSlot(Hooks::g_rightHandSlot);

        logger::debug(
            "[D2H EquipDebug] Unequip2H::thunk 2H weapon={} originalWeaponSlot={} Left={} Right={}",
            FormLabel(weapon),
            KnownSlotName(originalSlot),
            FormLabel(leftItem),
            FormLabel(rightItem));

        if (leftItem == a_form && a_form != rightItem) {
            logger::debug("[D2H EquipDebug] Unequip2H::thunk selecting LeftHand for same-base unequip");
            weapon->SetEquipSlot(Hooks::g_leftHandSlot);
        } else {
            logger::debug("[D2H EquipDebug] Unequip2H::thunk selecting RightHand for same-base/default unequip");
            weapon->SetEquipSlot(Hooks::g_rightHandSlot);
        }
    } else if (Hooks::isOneHanded(a_form) &&
        a_actor->GetEquippedObjectInSlot(Hooks::g_twoHandSlot) == a_form) {
        weapon = a_form->As<RE::TESObjectWEAP>();
        originalSlot = weapon->GetEquipSlot();

        logger::debug(
            "[D2H EquipDebug] Unequip2H::thunk 1H-as-2H weapon={} originalWeaponSlot={} -> TwoHand",
            FormLabel(weapon),
            KnownSlotName(originalSlot));
        weapon->SetEquipSlot(Hooks::g_twoHandSlot);
    }

    logger::debug(
        "[D2H EquipDebug] Unequip2H::thunk CALL ORIGINAL weaponCurrentSlot={}",
        weapon ? KnownSlotName(weapon->GetEquipSlot()) : "<not-weapon>");
    const std::int64_t result = func(a, a_actor, a_form, extraData);
    logger::debug("[D2H EquipDebug] Unequip2H::thunk RETURN ORIGINAL result={}", result);

    if (convertNormalToggleToTwoHand && normalToggleToTwoHandWeapon && Hooks::g_twoHandSlot) {
        logger::debug(
            "[D2H EquipDebug] Unequip2H normal-toggle->2H TRIGGER after vanilla unequip weapon={} -> TwoHand",
            FormLabel(normalToggleToTwoHandWeapon));

        const bool equipped = EquipInSlot(
            a_actor,
            normalToggleToTwoHandWeapon,
            nullptr,
            Hooks::g_twoHandSlot);

        logger::debug(
            "[D2H EquipDebug] Unequip2H normal-toggle->2H RESULT equipped={} originalUnequipResult={}",
            equipped,
            result);
        LogActorSlots(a_actor, "Unequip2H normal-toggle->2H AFTER CONVERSION");
    }

    if (weapon && originalSlot) {
        weapon->SetEquipSlot(originalSlot);
        logger::debug(
            "[D2H EquipDebug] Unequip2H::thunk restored weapon slot={} [{:08X}]",
            KnownSlotName(originalSlot),
            originalSlot->GetFormID());
    }

    LogActorSlots(a_actor, "Unequip2H::thunk EXIT");
    return result;
}
