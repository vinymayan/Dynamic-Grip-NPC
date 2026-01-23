#pragma once

#include "OAR/OpenAnimationReplacer-ConditionTypes.h"  
#include "OAR/OpenAnimationReplacerAPI-Conditions.h"  
#include "Hooks.h"

namespace Conditions {
    class IsEquipSlotOccupied : public CustomCondition {
    public:
        constexpr static inline std::string_view CONDITION_NAME = "IsWeaponSlotOccupied";

        IsEquipSlotOccupied();

        // --- Funções obrigatórias da interface ICondition ---
        RE::BSString GetName() const override { return CONDITION_NAME.data(); }
        RE::BSString GetDescription() const override {
            return "See which slot the weapon is using";
        }
        RE::BSString GetRequiredPluginAuthor() const override {
            return "Viny";
        }
        constexpr REL::Version GetRequiredVersion() const override { return { 0, 1, 0 }; }
        RE::BSString GetArgument() const override;

    protected:
        bool EvaluateImpl(RE::TESObjectREFR* a_refr, RE::hkbClipGenerator* a_clipGenerator,
            void* a_subMod) const override;

        // --- Componente da Condição ---
        // Voltamos a usar ITextConditionComponent
        ITextConditionComponent* slotNameComponent;
    };
}