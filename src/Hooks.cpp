#include "Hooks.h"
#include "Settings.h"

namespace {
    thread_local bool forcedGripOperation = false;

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

    bool EquipInSlot(RE::Actor* actor, RE::TESObjectWEAP* weapon, RE::ExtraDataList* extra, RE::BGSEquipSlot* slot) {
        auto* manager = RE::ActorEquipManager::GetSingleton();
        if (!manager || !actor || !weapon || !slot) return false;
        auto* originalSlot = weapon->GetEquipSlot();
        weapon->SetEquipSlot(slot);
        forcedGripOperation = true;
        manager->EquipObject(actor, weapon, extra, 1, slot);
        forcedGripOperation = false;
        weapon->SetEquipSlot(originalSlot);
        return true;
    }

    bool UnequipInSlot(RE::Actor* actor, RE::TESObjectWEAP* weapon, RE::ExtraDataList* extra, RE::BGSEquipSlot* slot) {
        auto* manager = RE::ActorEquipManager::GetSingleton();
        if (!manager || !actor || !weapon || !slot) return false;
        auto* originalSlot = weapon->GetEquipSlot();
        weapon->SetEquipSlot(slot);
        forcedGripOperation = true;
        manager->UnequipObject(actor, weapon, extra, 1, slot, false, true, true, true);
        forcedGripOperation = false;
        weapon->SetEquipSlot(originalSlot);
        return true;
    }
}

bool Hooks::CanEquipWithGrip(RE::Actor* actor, RE::TESObjectWEAP* weapon,
    DYNAMIC_TWO_HANDED_API::Grip grip, DYNAMIC_TWO_HANDED_API::Hand hand) {
    if (!actor || !weapon || !actor->IsHumanoid()) return false;
    if (grip == DYNAMIC_TWO_HANDED_API::Grip::kTwoHanded && hand != DYNAMIC_TWO_HANDED_API::Hand::kBoth) return false;

    const bool twoHandedWeapon = isTwoHanded(weapon);
    const bool oneHandedWeapon = isOneHanded(weapon);
    const bool keepsNativeTwoHanded = grip == DYNAMIC_TWO_HANDED_API::Grip::kTwoHanded && twoHandedWeapon;
    const bool convertsTwoToOne = grip == DYNAMIC_TWO_HANDED_API::Grip::kOneHanded && twoHandedWeapon;
    const bool convertsOneToTwo = grip == DYNAMIC_TWO_HANDED_API::Grip::kTwoHanded && oneHandedWeapon;
    if (!IsSupportedGripRequest(twoHandedWeapon, oneHandedWeapon, grip)) return false;

    // Explicit API requests take precedence over the normal-equip remapping option.
    if (keepsNativeTwoHanded) return true;

    if (hand == DYNAMIC_TWO_HANDED_API::Hand::kBoth && grip == DYNAMIC_TWO_HANDED_API::Grip::kOneHanded &&
        GetItemCount(actor, weapon) < 2) return false;

    if (!actor->IsPlayerRef()) return convertsTwoToOne && can2h(actor);
    if (!MeetsLevel(actor->GetLevel(), ModSettings::Settings.playerMinimumLevel)) return false;
    if (convertsTwoToOne) {
        return ModSettings::Settings.playerTwoHandedAsOneHanded &&
            ModSettings::HasRequiredPerk(actor, ModSettings::Settings.playerTwoHandedAsOneHandedPerk);
    }
    return ModSettings::Settings.playerOneHandedAsTwoHanded &&
        ModSettings::HasRequiredPerk(actor, ModSettings::Settings.playerOneHandedAsTwoHandedPerk);
}

bool Hooks::EquipWithGrip(RE::Actor* actor, RE::TESObjectWEAP* weapon, RE::ExtraDataList* extra,
    DYNAMIC_TWO_HANDED_API::Grip grip, DYNAMIC_TWO_HANDED_API::Hand hand) {
    if (!CanEquipWithGrip(actor, weapon, grip, hand)) return false;

    if (grip == DYNAMIC_TWO_HANDED_API::Grip::kOneHanded && hand == DYNAMIC_TWO_HANDED_API::Hand::kBoth) {
        return EquipInSlot(actor, weapon, extra, g_rightHandSlot) &&
            EquipInSlot(actor, weapon, extra, g_leftHandSlot);
    }

    auto* target = GripSlot(grip, hand);
    if (GetItemCount(actor, weapon) <= 1) {
        for (auto* slot : std::array{ g_rightHandSlot, g_leftHandSlot, g_twoHandSlot }) {
            if (slot && slot != target && IsEquippedInSlot(actor, weapon, slot)) {
                UnequipInSlot(actor, weapon, extra, slot);
            }
        }
    }
    return EquipInSlot(actor, weapon, extra, target);
}

bool Hooks::UnequipWithGrip(RE::Actor* actor, RE::TESObjectWEAP* weapon, RE::ExtraDataList* extra,
    DYNAMIC_TWO_HANDED_API::Grip grip, DYNAMIC_TWO_HANDED_API::Hand hand) {
    if (!actor || !weapon) return false;
    if (grip == DYNAMIC_TWO_HANDED_API::Grip::kOneHanded && hand == DYNAMIC_TWO_HANDED_API::Hand::kBoth) {
        const bool right = UnequipInSlot(actor, weapon, extra, g_rightHandSlot);
        const bool left = UnequipInSlot(actor, weapon, extra, g_leftHandSlot);
        return right || left;
    }
    return UnequipInSlot(actor, weapon, extra, GripSlot(grip, hand));
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
    if (forcedGripOperation) {
        return func(a, a_actor, a_form, extraData, count, equipSlot, queueEquip, forceEquip, playSounds, applyNow);
    }
    if (!a_actor) {
        return func(a, a_actor, a_form, extraData, count, equipSlot, queueEquip, forceEquip, playSounds, applyNow);
    }
    if (a_actor->IsPlayerRef()) {
        auto* weapon = a_form ? a_form->As<RE::TESObjectWEAP>() : nullptr;
        const auto hand = ModSettings::Settings.playerNormalTwoHandedHand == 1 ?
            DYNAMIC_TWO_HANDED_API::Hand::kLeft : DYNAMIC_TWO_HANDED_API::Hand::kRight;
        if (weapon && ModSettings::Settings.playerNormalTwoHandedAsOneHanded &&
            CanEquipWithGrip(a_actor, weapon, DYNAMIC_TWO_HANDED_API::Grip::kOneHanded, hand)) {
            auto* originalSlot = weapon->GetEquipSlot();
            weapon->SetEquipSlot(hand == DYNAMIC_TWO_HANDED_API::Hand::kLeft ? g_leftHandSlot : g_rightHandSlot);
            func(a, a_actor, a_form, extraData, count, equipSlot, false, true, playSounds, true);
            weapon->SetEquipSlot(originalSlot);
            return;
        }
        return func(a, a_actor, a_form, extraData, count, equipSlot, queueEquip, forceEquip, playSounds, applyNow);
    }
        if (Hooks::isTwoHanded(a_form)) {
            // Usa a fun��o can2h que j� criamos com os requisitos
            if (can2h(a_actor) && HasOffhandItem(a_actor, a_form)) {
                auto weapon = a_form->As<RE::TESObjectWEAP>();
                auto originalSlot = weapon->GetEquipSlot();

                weapon->SetEquipSlot(Hooks::g_rightHandSlot);
                func(a, a_actor, a_form, extraData, count, equipSlot, false, true, playSounds, true);
                weapon->SetEquipSlot(originalSlot);
                return;
            }
        }

    return func(a, a_actor, a_form, extraData, count, equipSlot, queueEquip, forceEquip, playSounds, applyNow);

}

std::int64_t Hooks::Unequip2H::thunk(std::int64_t* a, RE::Actor* a_actor, RE::TESForm* a_form,
    std::int64_t* extraData) {
    if (forcedGripOperation) {
        return func(a, a_actor, a_form, extraData);
    }
    if (!a_actor) {
        return func(a, a_actor, a_form, extraData);
    }
    RE::TESObjectWEAP* weapon = nullptr;
    RE::BGSEquipSlot* originalSlot = nullptr;
    // 1. VERIFICAR E ALTERAR (ANTES de chamar func)
    if (Hooks::isTwoHanded(a_form)) {
        weapon = a_form->As<RE::TESObjectWEAP>();
        originalSlot = weapon->GetEquipSlot();
        auto leftItem = a_actor->GetEquippedObjectInSlot(Hooks::g_leftHandSlot);
        auto rItem = a_actor->GetEquippedObjectInSlot(Hooks::g_rightHandSlot);

        if (leftItem == a_form && a_form != rItem) {
            weapon->SetEquipSlot(Hooks::g_leftHandSlot);
        }
        else {
            weapon->SetEquipSlot(Hooks::g_rightHandSlot);
        }
    } else if (Hooks::isOneHanded(a_form) && a_actor &&
        a_actor->GetEquippedObjectInSlot(Hooks::g_twoHandSlot) == a_form) {
        weapon = a_form->As<RE::TESObjectWEAP>();
        originalSlot = weapon->GetEquipSlot();
        weapon->SetEquipSlot(Hooks::g_twoHandSlot);
    }

    std::int64_t result = func(a, a_actor, a_form, extraData);

    if (weapon && originalSlot) {
        weapon->SetEquipSlot(originalSlot);
    }

    return result;
}
