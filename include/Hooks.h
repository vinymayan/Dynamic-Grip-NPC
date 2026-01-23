#pragma once

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
    void RegisterSinksForExistingCombatants();
    void Install();

    class NpcCombatTracker : public RE::BSTEventSink<RE::TESCombatEvent> {
    public:
        static NpcCombatTracker* GetSingleton() {
            static NpcCombatTracker singleton;
            return &singleton;
        }

        // Função chamada quando um evento de combate ocorre
        RE::BSEventNotifyControl ProcessEvent(const RE::TESCombatEvent* a_event,
            RE::BSTEventSource<RE::TESCombatEvent>*) override;

    private:
    };
}