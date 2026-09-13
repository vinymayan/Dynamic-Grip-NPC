#pragma once

#include "Dynamic2HAPI.h"

namespace Hooks {

    inline RE::BGSEquipSlot* g_leftHandSlot = nullptr;
    inline RE::BGSEquipSlot* g_rightHandSlot = nullptr;
    inline RE::BGSEquipSlot* g_twoHandSlot = nullptr;
    inline RE::BGSEquipSlot* g_shield = nullptr;

    struct Equip2H {
        static void thunk(std::int64_t* a, RE::Actor* a_actor, RE::TESForm* a_form, std::int64_t* extraData, int count,
            std::int64_t* equipSlot, char queueEquip, char forceEquip, char playSounds, char applyNow);
        static inline REL::Relocation<decltype(thunk)> func;
    };

    struct Unequip2H {
        static std::int64_t thunk(std::int64_t* a, RE::Actor* a_actor, RE::TESForm* a_form, std::int64_t* extraData);
        static inline REL::Relocation<decltype(thunk)> func;
    };
    bool isTwoHanded(RE::TESForm* a_weap);
    bool isOneHanded(RE::TESForm* a_weap);
    bool CanEquipWithGrip(RE::Actor* actor, RE::TESObjectWEAP* weapon,
        DYNAMIC_TWO_HANDED_API::Grip grip, DYNAMIC_TWO_HANDED_API::Hand hand);
    bool EquipWithGrip(RE::Actor* actor, RE::TESObjectWEAP* weapon, RE::ExtraDataList* extra,
        DYNAMIC_TWO_HANDED_API::Grip grip, DYNAMIC_TWO_HANDED_API::Hand hand);
    bool UnequipWithGrip(RE::Actor* actor, RE::TESObjectWEAP* weapon, RE::ExtraDataList* extra,
        DYNAMIC_TWO_HANDED_API::Grip grip, DYNAMIC_TWO_HANDED_API::Hand hand);
    void RegisterSinksForExistingCombatants();
    void Install();

    class NpcCombatTracker : public RE::BSTEventSink<RE::TESCombatEvent> {
    public:
        static NpcCombatTracker* GetSingleton() {
            static NpcCombatTracker singleton;
            return &singleton;
        }

        // Fun��o chamada quando um evento de combate ocorre
        RE::BSEventNotifyControl ProcessEvent(const RE::TESCombatEvent* a_event,
            RE::BSTEventSource<RE::TESCombatEvent>*) override;

    private:
    };
}
