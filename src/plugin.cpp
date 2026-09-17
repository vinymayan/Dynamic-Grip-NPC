#include "logger.h"
#include "Events.h"
#include "Settings.h"
#include "Manager.h"
#include "InventoryUI.h"
namespace fs = std::filesystem;

namespace {
    bool hasDFG = false;

    class DynamicFormsGeneratorListener : public RE::BSTEventSink<SKSE::ModCallbackEvent> {
    public:
        static DynamicFormsGeneratorListener* GetSingleton() {
            static DynamicFormsGeneratorListener singleton;
            return std::addressof(singleton);
        }

        void Register() {
            if (auto* dispatcher = SKSE::GetModCallbackEventSource()) dispatcher->AddEventSink(this);
        }

        RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* event,
            RE::BSTEventSource<SKSE::ModCallbackEvent>*) override {
            if (!event) return RE::BSEventNotifyControl::kContinue;
            const std::string_view name = event->eventName.c_str();
            if (name == "DynamicFormsGeneratorLoaded") {
                Manager::GetSingleton()->PopulateAllLists();
                ModSettings::LoadSE();
            } else if (name == "DynamicFormsGeneratorUpdated") {
                Manager::GetSingleton()->RefreshLists(event->strArg.c_str());
                ModSettings::LoadSE();
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };
}

template <typename T>
void RegisterCondition() {
    extern OAR_API::Conditions::IConditionsInterface* g_oarConditionsInterface;
    if (!g_oarConditionsInterface) {
        logger::error("OAR Conditions Interface nao disponivel para registrar {}", T::CONDITION_NAME);
        return;
    }

    switch (OAR_API::Conditions::AddCustomCondition<T>()) {
        using enum OAR_API::Conditions::APIResult;
    case OK:
        logger::info("Registrada condicao customizada: {}", T::CONDITION_NAME);
        break;
    case AlreadyRegistered:
        logger::warn("Condicao customizada {} ja registrada!", T::CONDITION_NAME);
        break;
    default:
        logger::error("Falha ao registrar condicao customizada {}!", T::CONDITION_NAME);
        break;
    }
}

void OnMessage(SKSE::MessagingInterface::Message* message) {
    if (message->type == SKSE::MessagingInterface::kPostLoad) {
        hasDFG = GetModuleHandleA("DynamicFormsGenerator.dll") != nullptr;
        logger::info("Dynamic Forms Generator {}", hasDFG ? "found" : "not found; using loaded perks only");
        OAR_API::Conditions::GetAPI();
        extern OAR_API::Conditions::IConditionsInterface* g_oarConditionsInterface; 

        if (g_oarConditionsInterface)
        {
            RegisterCondition<Conditions::IsEquipSlotOccupied>();
        }
        else {
            logger::error("Falha ao requisitar a API de Condicoes do OAR.");
        }
    }
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        auto dataHandler = RE::TESDataHandler::GetSingleton();
        Hooks::g_rightHandSlot = dataHandler->LookupForm<RE::BGSEquipSlot>(0x13f42, "Skyrim.esm");
        Hooks::g_leftHandSlot = dataHandler->LookupForm<RE::BGSEquipSlot>(0x13f43, "Skyrim.esm");
        Hooks::g_twoHandSlot = dataHandler->LookupForm<RE::BGSEquipSlot>(0x13f45, "Skyrim.esm");
        Hooks::g_shield = dataHandler->LookupForm<RE::BGSEquipSlot>(0x141E8, "Skyrim.esm");

        logger::debug(
            "[D2H EquipDebug] Equip slots loaded: Right={:08X} ptr={} Left={:08X} ptr={} TwoHand={:08X} ptr={} Shield={:08X} ptr={}",
            Hooks::g_rightHandSlot ? Hooks::g_rightHandSlot->GetFormID() : 0,
            static_cast<const void*>(Hooks::g_rightHandSlot),
            Hooks::g_leftHandSlot ? Hooks::g_leftHandSlot->GetFormID() : 0,
            static_cast<const void*>(Hooks::g_leftHandSlot),
            Hooks::g_twoHandSlot ? Hooks::g_twoHandSlot->GetFormID() : 0,
            static_cast<const void*>(Hooks::g_twoHandSlot),
            Hooks::g_shield ? Hooks::g_shield->GetFormID() : 0,
            static_cast<const void*>(Hooks::g_shield));

        Manager::GetSingleton()->PopulateAllLists();
        ModSettings::LoadSE();
        ModSettings::Register();
        InventoryUI::Register();
    }
    if (message->type == SKSE::MessagingInterface::kNewGame || message->type == SKSE::MessagingInterface::kPostLoadGame) {
        auto* NpcCycle = RE::ScriptEventSourceHolder::GetSingleton();
        if (NpcCycle) {
            NpcCycle->AddEventSink(Hooks::NpcCombatTracker::GetSingleton());
            SKSE::log::info("NpcCycleSink (All NPCs) registrado com sucesso.");
        }
        Hooks::RegisterSinksForExistingCombatants();
    }
}

SKSEPluginLoad(const SKSE::LoadInterface *skse) {

    SetupLog();
    logger::info("Plugin loaded");
    SKSE::Init(skse);
    Hooks::Install();
    DynamicFormsGeneratorListener::GetSingleton()->Register();
    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
    return true;
}
