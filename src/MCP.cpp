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
            logger::debug(
                "[D2H EquipDebug] API CanEquip ENTER actor={:08X} weapon={:08X} grip={} hand={}",
                actor ? actor->GetFormID() : 0,
                weapon ? weapon->GetFormID() : 0,
                static_cast<int>(grip),
                static_cast<int>(hand));
            const bool result = Hooks::CanEquipWithGrip(actor, weapon, grip, hand);
            logger::debug("[D2H EquipDebug] API CanEquip EXIT result={}", result);
            return result;
        }

        bool Equip(RE::Actor* actor, RE::TESObjectWEAP* weapon, RE::ExtraDataList* extra,
            DYNAMIC_TWO_HANDED_API::Grip grip, DYNAMIC_TWO_HANDED_API::Hand hand) noexcept override {
            logger::debug(
                "[D2H EquipDebug] API Equip ENTER actor={:08X} weapon={:08X} grip={} hand={} extra={}",
                actor ? actor->GetFormID() : 0,
                weapon ? weapon->GetFormID() : 0,
                static_cast<int>(grip),
                static_cast<int>(hand),
                static_cast<const void*>(extra));
            const bool result = Hooks::EquipWithGrip(actor, weapon, extra, grip, hand);
            logger::debug("[D2H EquipDebug] API Equip EXIT result={}", result);
            return result;
        }

        bool Unequip(RE::Actor* actor, RE::TESObjectWEAP* weapon, RE::ExtraDataList* extra,
            DYNAMIC_TWO_HANDED_API::Grip grip, DYNAMIC_TWO_HANDED_API::Hand hand) noexcept override {
            logger::debug(
                "[D2H EquipDebug] API Unequip ENTER actor={:08X} weapon={:08X} grip={} hand={} extra={}",
                actor ? actor->GetFormID() : 0,
                weapon ? weapon->GetFormID() : 0,
                static_cast<int>(grip),
                static_cast<int>(hand),
                static_cast<const void*>(extra));
            const bool result = Hooks::UnequipWithGrip(actor, weapon, extra, grip, hand);
            logger::debug("[D2H EquipDebug] API Unequip EXIT result={}", result);
            return result;
        }
    };
}

extern "C" __declspec(dllexport) DYNAMIC_TWO_HANDED_API::IDynamic2H1* GetDynamic2HAPI(
    std::uint32_t version) noexcept {
    static Dynamic2HImplementation implementation;
    const bool accepted = version == DYNAMIC_TWO_HANDED_API::API_VERSION;
    logger::debug(
        "[D2H EquipDebug] GetDynamic2HAPI requestedVersion={} supportedVersion={} accepted={}",
        version,
        DYNAMIC_TWO_HANDED_API::API_VERSION,
        accepted);
    return accepted ? std::addressof(implementation) : nullptr;
}
