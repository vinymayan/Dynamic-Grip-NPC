#include "Dynamic2HAPI.h"
#include "Hooks.h"

namespace {
    class Dynamic2HImplementation final : public DYNAMIC_TWO_HANDED_API::IDynamic2H1 {
    public:
        std::uint32_t GetVersion() const noexcept override {
            return DYNAMIC_TWO_HANDED_API::API_VERSION;
        }

        bool CanEquip(RE::Actor* actor, RE::TESObjectWEAP* weapon,
            DYNAMIC_TWO_HANDED_API::Grip grip, DYNAMIC_TWO_HANDED_API::Hand hand) const noexcept override {
            return Hooks::CanEquipWithGrip(actor, weapon, grip, hand);
        }

        bool Equip(RE::Actor* actor, RE::TESObjectWEAP* weapon, RE::ExtraDataList* extra,
            DYNAMIC_TWO_HANDED_API::Grip grip, DYNAMIC_TWO_HANDED_API::Hand hand) noexcept override {
            return Hooks::EquipWithGrip(actor, weapon, extra, grip, hand);
        }

        bool Unequip(RE::Actor* actor, RE::TESObjectWEAP* weapon, RE::ExtraDataList* extra,
            DYNAMIC_TWO_HANDED_API::Grip grip, DYNAMIC_TWO_HANDED_API::Hand hand) noexcept override {
            return Hooks::UnequipWithGrip(actor, weapon, extra, grip, hand);
        }
    };
}

extern "C" __declspec(dllexport) DYNAMIC_TWO_HANDED_API::IDynamic2H1* GetDynamic2HAPI(
    std::uint32_t version) noexcept {
    static Dynamic2HImplementation implementation;
    return version == DYNAMIC_TWO_HANDED_API::API_VERSION ? std::addressof(implementation) : nullptr;
}
