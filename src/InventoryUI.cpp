#include "InventoryUI.h"

#include "Hooks.h"

#include "RE/A/Actor.h"
#include "RE/C/ContainerMenu.h"
#include "RE/I/InventoryEntryData.h"
#include "RE/I/InventoryMenu.h"
#include "RE/I/ItemList.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESObjectREFR.h"

namespace InventoryUI {
    namespace {
        struct EntryContext {
            RE::InventoryEntryData* entryData = nullptr;
            RE::Actor* ownerActor = nullptr;
        };

        RE::TESObjectREFR* ResolveOwner(const RE::StandardItemData& data, RE::TESObjectREFR* fallback)
        {
            if (auto owner = RE::TESObjectREFR::LookupByHandle(data.owner); owner) {
                return owner.get();
            }
            return fallback;
        }

        EntryContext GetEntryContextAtIndex(std::int32_t index)
        {
            if (index < 0) {
                return {};
            }

            auto* ui = RE::UI::GetSingleton();
            if (!ui) {
                return {};
            }

            RE::ItemList* itemList = nullptr;
            RE::TESObjectREFR* fallbackOwner = nullptr;

            // ContainerMenu is used for followers/NPCs/containers and also for the
            // player side of a trade. StandardItemData::owner tells us which actor
            // actually owns each row, so always prefer it over the menu target.
            if (ui->IsMenuOpen(RE::ContainerMenu::MENU_NAME)) {
                if (auto menu = ui->GetMenu<RE::ContainerMenu>()) {
                    itemList = menu->GetRuntimeData().itemList;
                    if (auto target = RE::TESObjectREFR::LookupByHandle(RE::ContainerMenu::GetTargetRefHandle()); target) {
                        fallbackOwner = target.get();
                    }
                }
            } else if (ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME)) {
                if (auto menu = ui->GetMenu<RE::InventoryMenu>()) {
                    itemList = menu->GetRuntimeData().itemList;
                    fallbackOwner = RE::PlayerCharacter::GetSingleton();
                }
            }

            if (!itemList) {
                return {};
            }

            auto& items = itemList->items;
            const auto itemIndex = static_cast<std::size_t>(index);
            if (itemIndex >= items.size() || !items[itemIndex]) {
                return {};
            }

            auto& data = items[itemIndex]->data;
            if (!data.objDesc) {
                return {};
            }

            auto* owner = ResolveOwner(data, fallbackOwner);
            return {
                data.objDesc,
                owner ? owner->As<RE::Actor>() : nullptr
            };
        }

        const char* GetEquipState(RE::InventoryEntryData* entryData, RE::Actor* ownerActor)
        {
            if (!entryData || !entryData->object || !entryData->object->IsWeapon()) {
                // Empty means the injected SWF should leave vanilla/non-weapon state alone.
                return "";
            }

            // Generic containers do not have equip state. Leaving the value empty
            // prevents the injected SWF from overriding the vanilla icon there.
            if (!ownerActor) {
                return "";
            }

            auto* object = entryData->object;

            // Dynamic2H can place weapons in any of these three actual equip slots.
            // Read the real owner of the current row instead of assuming Player.
            const bool inRight = Hooks::g_rightHandSlot &&
                ownerActor->GetEquippedObjectInSlot(Hooks::g_rightHandSlot) == object;
            const bool inLeft = Hooks::g_leftHandSlot &&
                ownerActor->GetEquippedObjectInSlot(Hooks::g_leftHandSlot) == object;
            const bool inTwoHand = Hooks::g_twoHandSlot &&
                ownerActor->GetEquippedObjectInSlot(Hooks::g_twoHandSlot) == object;

            // Same base weapon in both hands (two copies), including Dynamic2H dual-wield 2H.
            if (inRight && inLeft) {
                return "LeftAndRightEquip";
            }

            // Different weapons dual-wielded are handled per inventory row: one reports left,
            // the other reports right. This also covers 2H weapons converted to one-handed grip.
            if (inLeft) {
                return "LeftEquip";
            }
            if (inRight) {
                return "RightEquip";
            }

            // Native 2H, and 1H weapons explicitly converted by Dynamic2H to the two-hand slot.
            if (inTwoHand) {
                return "Equipped";
            }

            // Explicitly clear the injected icon for unequipped weapons.
            return "None";
        }

        class GetEquipStateHandler final : public RE::GFxFunctionHandler {
        public:
            void Call(Params& params) override
            {
                if (!params.retVal) {
                    return;
                }

                // Default: do not touch the row if the call is invalid/non-weapon.
                params.retVal->SetString("");

                if (params.argCount != 1 || !params.args || !params.args[0].IsNumber()) {
                    return;
                }

                const auto index = static_cast<std::int32_t>(params.args[0].GetUInt());
                const auto context = GetEntryContextAtIndex(index);
                if (!context.entryData) {
                    return;
                }

                params.retVal->SetString(GetEquipState(context.entryData, context.ownerActor));
            }
        };

        void Inject(const RE::BSFixedString& menuName)
        {
            auto* ui = RE::UI::GetSingleton();
            if (!ui) {
                return;
            }

            RE::GPtr<RE::IMenu> menu = ui->GetMenu(menuName);
            if (!menu || !menu->uiMovie) {
                return;
            }

            auto movie = menu->uiMovie;

            RE::GFxValue root;
            if (!movie->GetVariable(&root, "_root")) {
                return;
            }

            RE::GFxValue getEquipState;
            movie->CreateFunction(&getEquipState, new GetEquipStateHandler());
            root.SetMember("D2H_GetEquipState", getEquipState);

            RE::GFxValue createArgs[2];
            createArgs[0] = RE::GFxValue("D2H");
            createArgs[1] = RE::GFxValue(2569);
            root.Invoke("createEmptyMovieClip", nullptr, createArgs, 2);

            RE::GFxValue d2hClip;
            if (movie->GetVariable(&d2hClip, "_root.D2H")) {
                RE::GFxValue loadArgs[1];
                loadArgs[0] = RE::GFxValue("Viny_Dynamic2H.swf");
                d2hClip.Invoke("loadMovie", nullptr, loadArgs, 1);
                logger::debug("Dynamic2H equip-state UI injected");
            }
        }

        class UISink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
        public:
            static UISink* GetSingleton()
            {
                static UISink singleton;
                return std::addressof(singleton);
            }

            RE::BSEventNotifyControl ProcessEvent(
                const RE::MenuOpenCloseEvent* event,
                RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
            {
                if (event && event->opening &&
                    (event->menuName == RE::InventoryMenu::MENU_NAME ||
                     event->menuName == RE::ContainerMenu::MENU_NAME)) {
                    Inject(event->menuName);
                }

                return RE::BSEventNotifyControl::kContinue;
            }
        };
    }

    void Register()
    {
        auto* ui = RE::UI::GetSingleton();
        if (!ui) {
            logger::error("Failed to register Dynamic2H inventory UI: UI singleton unavailable");
            return;
        }

        ui->AddEventSink(UISink::GetSingleton());
        logger::info("Dynamic2H inventory/container UI sink registered");
    }
}
