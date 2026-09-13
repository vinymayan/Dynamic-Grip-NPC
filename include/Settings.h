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
        bool playerNormalTwoHandedAsOneHanded = false;
        int playerNormalTwoHandedHand = 0;
        bool playerOneHandedAsTwoHanded = false;
        RE::FormID playerOneHandedAsTwoHandedPerk = 0;
    };

    inline THConfig Settings;

    void ModMenu();
    void PlayerMenu();
    void Register();
    void LoadSE();
    void SaveSe();
    bool HasRequiredPerk(RE::Actor* actor, RE::FormID perkID);
}
