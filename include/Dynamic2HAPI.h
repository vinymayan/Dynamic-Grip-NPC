#pragma once

#include <Windows.h>

#include <cstdint>

namespace RE {
    class Actor;
    class ExtraDataList;
    class TESObjectWEAP;
}

namespace DYNAMIC_TWO_HANDED_API {
    inline constexpr std::uint32_t API_VERSION = 1;
    inline constexpr auto DLL_NAME = L"Dynamic2H.dll";

    enum class Grip : std::uint8_t { kOneHanded, kTwoHanded };
    enum class Hand : std::uint8_t { kRight, kLeft, kBoth };

    class IDynamic2H1 {
    public:
        virtual ~IDynamic2H1() = default;
        virtual std::uint32_t GetVersion() const noexcept = 0;
        virtual bool CanEquip(RE::Actor* actor, RE::TESObjectWEAP* weapon, Grip grip, Hand hand) const noexcept = 0;
        virtual bool Equip(RE::Actor* actor, RE::TESObjectWEAP* weapon, RE::ExtraDataList* extra, Grip grip, Hand hand) noexcept = 0;
        virtual bool Unequip(RE::Actor* actor, RE::TESObjectWEAP* weapon, RE::ExtraDataList* extra, Grip grip, Hand hand) noexcept = 0;
    };

    using RequestAPI = IDynamic2H1* (*)(std::uint32_t);

    [[nodiscard]] inline IDynamic2H1* RequestInterface() noexcept {
        const auto module = ::GetModuleHandleW(DLL_NAME);
        if (!module) {
            return nullptr;
        }
        const auto request = reinterpret_cast<RequestAPI>(::GetProcAddress(module, "GetDynamic2HAPI"));
        return request ? request(API_VERSION) : nullptr;
    }
}
