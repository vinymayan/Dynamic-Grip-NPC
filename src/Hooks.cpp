#include "Hooks.h"
#include "Settings.h"

auto player = RE::PlayerCharacter::GetSingleton();

bool Hooks::isTwoHanded(RE::TESForm* a_weap) {
    if (!a_weap || !a_weap->IsWeapon()) return false;
    auto weap = a_weap->As<RE::TESObjectWEAP>();
    if (weap->IsTwoHandedSword() || weap->IsTwoHandedAxe()) return true;
    return false;
}

bool isOneHanded(RE::TESForm* a_weap) {
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

    // Verifica Nível do Personagem
    int actorLevel = actor->GetLevel();

    // Verifica Valor da Skill TwoHanded
    float skillValue = actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kTwoHanded);

    bool meetsRequirements = (actorLevel >= ModSettings::Settings.minimumLevel && skillValue >= ModSettings::Settings.skillValue);

    if (!meetsRequirements) {
        logger::debug("  - NPC '{}' falhou nos requisitos: Level {}/{} , Skill 2H {}/{}",
            actor->GetName(), actorLevel, ModSettings::Settings.minimumLevel, skillValue, ModSettings::Settings.skillValue);
    }

    return meetsRequirements;
}

bool HasOffhandItem(RE::Actor* actor, RE::TESForm* itemBeingEquipped) {
    if (!actor) return false;

    auto inventory = actor->GetInventory();
    for (const auto& [item, data] : inventory) {
        int count = data.first;
        if (count <= 0) continue;

        // Se o item no inventário for o mesmo que estamos tentando equipar, 
        // precisamos ter pelo menos 2 dele para sobrar um para a outra mão.
        if (item == itemBeingEquipped) {
            count--;
        }

        if (count > 0) {
            if (Hooks::isTwoHanded(item) || isOneHanded(item) || isShield(item)) {
				logger::debug("  - Item disponível para a mão esquerda encontrado: '{}' (ID: {})", item->GetName(), item->GetFormID());
                return true;
            }
        }
    }
    return false;
}

void CheckForNPC(RE::Actor* npc) {
    if (!npc || npc->IsPlayerRef() || !can2h(npc)) {
        logger::debug("--- [CheckAndEquipHandle] NPC inválido : '{}'", npc->GetName());
            return;
    }

    auto equipManager = RE::ActorEquipManager::GetSingleton();
    if (!equipManager) {
        return;
    }
	logger::debug("--- [CheckAndEquipHandle] Início da verificação para NPC: '{}' (ID: {}) ---", npc->GetName(), npc->GetFormID());
    // 1. Obter o status atual do equipamento
    auto equippedItemL = npc->GetEquippedObjectInSlot(Hooks::g_leftHandSlot);
    auto equippedItemR = npc->GetEquippedObjectInSlot(Hooks::g_rightHandSlot);
    auto equippedItem2H = npc->GetEquippedObjectInSlot(Hooks::g_twoHandSlot);

    bool isR_2H = Hooks::isTwoHanded(equippedItemR);
    bool isL_2H = Hooks::isTwoHanded(equippedItemL);
    bool is2H_2H = Hooks::isTwoHanded(equippedItem2H);

    // 2. Verificações de saída antecipada
    if (isL_2H && isR_2H) {
        //logger::info("  - Status: NPC já está empunhando duas armas de duas mãos (Dual 2H).");
        //logger::info("--- [CheckAndEquipHandle] Fim da verificação ---");
        return;  // Já está em dual wielding 2H
    }

    if (isR_2H && equippedItemL != nullptr) {
        std::string leftItemName = "Item Desconhecido";
        if (equippedItemL) {
            leftItemName = equippedItemL->GetName();
        }
        //logger::info("  - Status: NPC já está empunhando 2H na direita e '{}' na esquerda. Não interferir.",leftItemName);
        //logger::info("--- [CheckAndEquipHandle] Fim da verificação ---");
        return;
    }

    // 4. Escanear inventário para itens NÃO EQUIPADOS
    /*logger::info("  - Escaneando inventário...");*/
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
            else if (isOneHanded(item)) {
                available1H.push_back(item->As<RE::TESObjectWEAP>());
            }
            else if (isShield(item)) {
                availableShields.push_back(item->As<RE::TESObjectARMO>());
            }
        }
    }

    int total2HCount = (int)available2H.size() + (isR_2H ? 1 : 0) + (isL_2H ? 1 : 0) + (is2H_2H ? 1 : 0);
	logger::debug("  - NPC possui {} armas de 2 mãos disponíveis no inventário (incluindo as equipadas).", total2HCount);
    // Se ele só tem uma 2H E não tem absolutamente nada para a mão esquerda (1H, Escudo ou algo já equipado)
    bool nothingForLeftHand = available1H.empty() && availableShields.empty();
	logger::debug("    - Itens disponíveis para a mão esquerda: 1H = {}, Escudos = {}.", available1H.size(), availableShields.size());
    if (total2HCount <= 1 && nothingForLeftHand) {
        logger::debug("  - Status: NPC só possui uma 2H e nada para a mão esquerda. Mantendo equipamento original.");
        return;
    }

    RE::TESObjectWEAP* weaponForRight = nullptr;

    // PASSO 1: Garantir Arma de 2 Mãos na Direita
    if (Hooks::isTwoHanded(equippedItemR)) {
        weaponForRight = equippedItemR->As<RE::TESObjectWEAP>();
    }
    else if (Hooks::isTwoHanded(equippedItem2H)) {
        // Tem uma 2H no slot vanilla: MOVE para a mão direita
        weaponForRight = equippedItem2H->As<RE::TESObjectWEAP>();
        equipManager->UnequipObject(npc, weaponForRight, nullptr, 1, Hooks::g_twoHandSlot, false, true, true);
        EquipItemWithGripChange(npc, weaponForRight, Hooks::g_rightHandSlot);
    }
    else if (!available2H.empty()) {
        weaponForRight = available2H[0];
        available2H.erase(available2H.begin()); // Removemos pois será usada
        EquipItemWithGripChange(npc, weaponForRight, Hooks::g_rightHandSlot);
    }

    // Se não conseguimos uma 2H para a direita, paramos a lógica de "Dual 2H" aqui
    if (!weaponForRight) return;

    // PASSO 2: Tentar Arma de 2 Mãos na Esquerda
    bool isLeftAlready2H = Hooks::isTwoHanded(equippedItemL);

    if (!isLeftAlready2H) {
        if (!available2H.empty()) {
            // Prioridade: Equipar segunda arma de 2 mãos
            EquipItemWithGripChange(npc, available2H[0], Hooks::g_leftHandSlot);
        }
        else if (equippedItemL == nullptr) {
            // Fallback: Apenas se a mão esquerda estiver vazia
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
    SKSE::log::info("[NpcCombatTracker] Verificando NPCs já em combate após carregar o jogo...");

    auto* processLists = RE::ProcessLists::GetSingleton();
    if (!processLists) {
        SKSE::log::warn("[NpcCombatTracker] Não foi possível obter ProcessLists.");
        return;
    }

    // Itera sobre todos os atores que estão "ativos" no jogo
    for (auto& actorHandle : processLists->highActorHandles) {
        if (auto actor = actorHandle.get().get()) {
            // A função IsInCombat() nos diz se o ator já está em um estado de combate
            if (actor != player) {
                CheckForNPC(actor);
            }

        }
    }
    SKSE::log::info("[NpcCombatTracker] Verificação concluída.");
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
    if (!a_actor || a_actor->IsPlayerRef()) {
        return func(a, a_actor, a_form, extraData, count, equipSlot, queueEquip, forceEquip, playSounds, applyNow);
    }
        if (Hooks::isTwoHanded(a_form)) {
            // Usa a função can2h que já criamos com os requisitos
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
    RE::TESObjectWEAP* weapon = nullptr;
    RE::BGSEquipSlot* originalSlot = nullptr;
    RE::TESBoundObject* a_bound = a_form ? a_form->As<RE::TESBoundObject>() : nullptr;
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
    }

    std::int64_t result = func(a, a_actor, a_form, extraData);

    if (weapon && originalSlot) {
        weapon->SetEquipSlot(originalSlot);
    }

    return result;
}