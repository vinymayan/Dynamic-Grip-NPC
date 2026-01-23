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
    };

    inline THConfig Settings;

    void ModMenu();
    void Register();
    void LoadSE();
    void SaveSe();
}