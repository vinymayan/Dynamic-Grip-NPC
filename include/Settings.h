#pragma once
#include "rapidjson/document.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/filewritestream.h"
#include "rapidjson/writer.h"
#include "SKSEMCP/SKSEMenuFramework.hpp"


namespace ModSettings {

    struct THConfig {
        int minimumLevel = 0;
        float skillValue = 5.0f;
        RE::FormID npcRequiredPerk = 0;
        int playerMinimumLevel = 0;
        bool playerTwoHandedAsOneHanded = false;
        RE::FormID playerTwoHandedAsOneHandedPerk = 0;
        // Separate requirements for equipping two native 2H weapons simultaneously.
        // These are additive to the regular 2H -> 1H level and perk requirements.
        int playerDualTwoHandedMinimumLevel = 0;
        RE::FormID playerDualTwoHandedPerk = 0;
        bool playerNormalTwoHandedAsOneHanded = false;
        bool playerOneHandedAsTwoHanded = false;
        RE::FormID playerOneHandedAsTwoHandedPerk = 0;
        // Normal Skyrim Inventory/Favorites second equip/toggle only, for either
        // native 1H or native 2H weapons being held in a one-hand slot.
        // Explicit Dynamic2H API Equip/Unequip requests never consult this option.
        bool playerNormalOneHandedSecondEquipAsTwoHanded = false;
    };

    inline THConfig Settings;

    void ModMenu();
    void PlayerMenu();
    void Register();
    void LoadSE();
    void SaveSe();
    bool HasRequiredPerk(RE::Actor* actor, RE::FormID perkID);
}
