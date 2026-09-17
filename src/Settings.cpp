#include "Settings.h"
#include "Manager.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>

// The old file is retained as a backup; it is imported once if the new file is absent.
namespace {
    constexpr const char* kSettingsPath = "Data/Viny Mods/Dynamic Two-Handed/Settings.json";
    constexpr const char* kLegacySettingsPath = "Data/SKSE/Plugins/Dynamic Two-Handed.json";
    constexpr const char* kLanguagePath = "Data/Viny Mods/Dynamic Two-Handed/Language.json";
}

namespace ModSettings {

    namespace {
        // Like Parry for All: editable, nested { "menu": { "key": "text" } } language file.
        // Pointers returned by GetLoc stay valid because this map is populated once on registration.
        std::unordered_map<std::string, std::string> languageTexts;

        void LoadLanguage() {
            languageTexts.clear();
            std::ifstream file(kLanguagePath, std::ios::binary);
            if (!file) {
                logger::warn("[D2H Settings] Language file '{}' not found; using English defaults", kLanguagePath);
                return;
            }

            std::string text(std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{});
            if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
                static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
                text.erase(0, 3);
            }
            rapidjson::Document doc;
            doc.Parse(text.c_str());
            if (doc.HasParseError() || !doc.IsObject()) {
                logger::warn("[D2H Settings] Invalid Language.json '{}'; using English defaults", kLanguagePath);
                return;
            }
            for (auto entry = doc.MemberBegin(); entry != doc.MemberEnd(); ++entry) {
                if (entry->value.IsString()) {
                    languageTexts.emplace(entry->name.GetString(), entry->value.GetString());
                } else if (entry->value.IsObject()) {
                    for (auto child = entry->value.MemberBegin(); child != entry->value.MemberEnd(); ++child) {
                        if (child->value.IsString()) {
                            languageTexts.emplace(std::string(entry->name.GetString()) + "." + child->name.GetString(),
                                child->value.GetString());
                        }
                    }
                }
            }
            logger::info("[D2H Settings] Loaded {} translations from '{}'", languageTexts.size(), kLanguagePath);
        }

        const char* GetLoc(const char* key, const char* defaultText) {
            const auto found = languageTexts.find(key);
            return found == languageTexts.end() ? defaultText : found->second.c_str();
        }

        bool DrawPerkDropdown(const char* label, RE::FormID& selectedPerk) {
            const auto& perks = Manager::GetSingleton()->GetPerks();
            std::vector<const char*> names{ GetLoc("menu.none", "None") };
            names.reserve(perks.size() + 1);
            int selected = 0;
            for (std::size_t i = 0; i < perks.size(); ++i) {
                names.push_back(perks[i].displayName.c_str());
                if (perks[i].formID == selectedPerk) selected = static_cast<int>(i + 1);
            }
            ImGuiMCP::SetNextItemWidth(320.0f);
            if (!ImGuiMCP::Combo(label, &selected, names.data(), static_cast<int>(names.size()))) return false;
            selectedPerk = selected == 0 ? 0 : perks[static_cast<std::size_t>(selected - 1)].formID;
            return true;
        }

        void AddFormID(rapidjson::Document& doc, const char* key, RE::FormID id) {
            auto& allocator = doc.GetAllocator();
            auto* form = RE::TESForm::LookupByID(id);
            const auto normalized = FormUtil::NormalizeFormID(form);
            rapidjson::Value jsonKey(key, allocator);
            rapidjson::Value jsonValue(normalized.c_str(), allocator);
            doc.AddMember(jsonKey, jsonValue, allocator);
            if (form) {
                const auto* editorID = form->GetFormEditorID();
                if (editorID && editorID[0]) {
                    const auto editorKey = std::string(key) + "EditorID";
                    doc.AddMember(
                        rapidjson::Value(editorKey.c_str(), allocator),
                        rapidjson::Value(editorID, allocator),
                        allocator);
                }
            }
        }

        void ReadFormID(const rapidjson::Document& doc, const char* key, RE::FormID& id) {
            const auto editorKey = std::string(key) + "EditorID";
            if (doc.HasMember(editorKey.c_str()) && doc[editorKey.c_str()].IsString()) {
                if (auto* form = RE::TESForm::LookupByEditorID(doc[editorKey.c_str()].GetString())) {
                    id = form->GetFormID();
                    return;
                }
            }
            if (doc.HasMember(key) && doc[key].IsString()) {
                id = FormUtil::FormIDFromString(doc[key].GetString());
            }
        }
    }

    bool WriteSettings() {
        rapidjson::Document doc;
        doc.SetObject();
        auto& allocator = doc.GetAllocator();

        // Adiciona os membros baseados na struct THConfig
        doc.AddMember("minimumLevel", Settings.minimumLevel, allocator);
        doc.AddMember("skillValue", Settings.skillValue, allocator);
        AddFormID(doc, "npcRequiredPerk", Settings.npcRequiredPerk);
        doc.AddMember("playerMinimumLevel", Settings.playerMinimumLevel, allocator);
        doc.AddMember("playerTwoHandedAsOneHanded", Settings.playerTwoHandedAsOneHanded, allocator);
        AddFormID(doc, "playerTwoHandedAsOneHandedPerk", Settings.playerTwoHandedAsOneHandedPerk);
        doc.AddMember("playerDualTwoHandedMinimumLevel", Settings.playerDualTwoHandedMinimumLevel, allocator);
        AddFormID(doc, "playerDualTwoHandedPerk", Settings.playerDualTwoHandedPerk);
        doc.AddMember("playerNormalTwoHandedAsOneHanded", Settings.playerNormalTwoHandedAsOneHanded, allocator);
        doc.AddMember("playerOneHandedAsTwoHanded", Settings.playerOneHandedAsTwoHanded, allocator);
        AddFormID(doc, "playerOneHandedAsTwoHandedPerk", Settings.playerOneHandedAsTwoHandedPerk);
        doc.AddMember(
            "playerNormalOneHandedSecondEquipAsTwoHanded",
            Settings.playerNormalOneHandedSecondEquipAsTwoHanded,
            allocator);

        const std::filesystem::path path(kSettingsPath);
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            logger::error("[D2H Settings] Failed to create settings folder: {}", error.message());
            return false;
        }

        FILE* fp = nullptr;
        if (fopen_s(&fp, kSettingsPath, "wb") == 0 && fp) {
            char writeBuffer[65536];
            rapidjson::FileWriteStream os(fp, writeBuffer, sizeof(writeBuffer));
            rapidjson::Writer<rapidjson::FileWriteStream> writer(os);
            const bool wrote = doc.Accept(writer);
            const int closed = fclose(fp);
            if (!wrote || closed != 0) {
                logger::error("[D2H Settings] Failed to write '{}'", kSettingsPath);
                return false;
            }
            logger::debug(
                "[D2H EquipDebug] Settings SAVED: playerMinimumLevel={} Enable2HAs1H={} "
                "2HAs1HPerk={:08X} Normal2HUses1HSlot={} Enable1HAs2H={} 1HAs2HPerk={:08X} "
                "NormalSecondEquipAs2H={} Dual2HLevel={} Dual2HPerk={:08X}",
                Settings.playerMinimumLevel,
                Settings.playerTwoHandedAsOneHanded,
                Settings.playerTwoHandedAsOneHandedPerk,
                Settings.playerNormalTwoHandedAsOneHanded,
                Settings.playerOneHandedAsTwoHanded,
                Settings.playerOneHandedAsTwoHandedPerk,
                Settings.playerNormalOneHandedSecondEquipAsTwoHanded,
                Settings.playerDualTwoHandedMinimumLevel,
                Settings.playerDualTwoHandedPerk);
            return true;
        }
        logger::error("[D2H Settings] Failed to open '{}' for writing", kSettingsPath);
        return false;
    }

    void SaveSe() {
        (void)WriteSettings();
    }

    // Strict import: never replace an existing new file with stale legacy data.
    bool ReadSettings(const char* path) {
        FILE* fp = nullptr;
        if (fopen_s(&fp, path, "rb") == 0 && fp) {
            char readBuffer[65536];
            rapidjson::FileReadStream is(fp, readBuffer, sizeof(readBuffer));
            rapidjson::Document doc;
            doc.ParseStream(is);
            fclose(fp);

            if (doc.HasParseError() || !doc.IsObject()) {
                logger::error("[D2H Settings] Invalid settings JSON in '{}'; preserving current settings", path);
                return false;
            }
            if (doc.IsObject()) {
                if (doc.HasMember("minimumLevel") && doc["minimumLevel"].IsInt())
                    Settings.minimumLevel = doc["minimumLevel"].GetInt();

                if (doc.HasMember("skillValue") && doc["skillValue"].IsNumber())
                    Settings.skillValue = doc["skillValue"].GetFloat();

                ReadFormID(doc, "npcRequiredPerk", Settings.npcRequiredPerk);
                if (doc.HasMember("playerMinimumLevel") && doc["playerMinimumLevel"].IsInt())
                    Settings.playerMinimumLevel = std::clamp(doc["playerMinimumLevel"].GetInt(), 0, 255);
                if (doc.HasMember("playerTwoHandedAsOneHanded") && doc["playerTwoHandedAsOneHanded"].IsBool())
                    Settings.playerTwoHandedAsOneHanded = doc["playerTwoHandedAsOneHanded"].GetBool();
                ReadFormID(doc, "playerTwoHandedAsOneHandedPerk", Settings.playerTwoHandedAsOneHandedPerk);
                if (doc.HasMember("playerDualTwoHandedMinimumLevel") && doc["playerDualTwoHandedMinimumLevel"].IsInt())
                    Settings.playerDualTwoHandedMinimumLevel = std::clamp(doc["playerDualTwoHandedMinimumLevel"].GetInt(), 0, 255);
                ReadFormID(doc, "playerDualTwoHandedPerk", Settings.playerDualTwoHandedPerk);
                if (doc.HasMember("playerNormalTwoHandedAsOneHanded") && doc["playerNormalTwoHandedAsOneHanded"].IsBool())
                    Settings.playerNormalTwoHandedAsOneHanded = doc["playerNormalTwoHandedAsOneHanded"].GetBool();
                if (doc.HasMember("playerOneHandedAsTwoHanded") && doc["playerOneHandedAsTwoHanded"].IsBool())
                    Settings.playerOneHandedAsTwoHanded = doc["playerOneHandedAsTwoHanded"].GetBool();
                ReadFormID(doc, "playerOneHandedAsTwoHandedPerk", Settings.playerOneHandedAsTwoHandedPerk);
                if (doc.HasMember("playerNormalOneHandedSecondEquipAsTwoHanded") &&
                    doc["playerNormalOneHandedSecondEquipAsTwoHanded"].IsBool()) {
                    Settings.playerNormalOneHandedSecondEquipAsTwoHanded =
                        doc["playerNormalOneHandedSecondEquipAsTwoHanded"].GetBool();
                }
            }

            logger::debug(
                "[D2H EquipDebug] Settings LOADED: playerMinimumLevel={} Enable2HAs1H={} "
                "2HAs1HPerk={:08X} Normal2HUses1HSlot={} Enable1HAs2H={} 1HAs2HPerk={:08X} "
                "NormalSecondEquipAs2H={} Dual2HLevel={} Dual2HPerk={:08X}",
                Settings.playerMinimumLevel,
                Settings.playerTwoHandedAsOneHanded,
                Settings.playerTwoHandedAsOneHandedPerk,
                Settings.playerNormalTwoHandedAsOneHanded,
                Settings.playerOneHandedAsTwoHanded,
                Settings.playerOneHandedAsTwoHandedPerk,
                Settings.playerNormalOneHandedSecondEquipAsTwoHanded,
                Settings.playerDualTwoHandedMinimumLevel,
                Settings.playerDualTwoHandedPerk);
            logger::info("[D2H Settings] Loaded settings from '{}'", path);
            return true;
        }
        logger::warn("[D2H Settings] Could not open '{}' for reading", path);
        return false;
    }

    void LoadSE() {
        std::error_code error;
        const bool hasNewSettings = std::filesystem::exists(kSettingsPath, error);
        if (error) {
            logger::error("[D2H Settings] Cannot check new settings path: {}", error.message());
            return;
        }
        if (hasNewSettings) {
            // An invalid new file is not overwritten or silently replaced by the old one.
            (void)ReadSettings(kSettingsPath);
            return;
        }
        error.clear();
        const bool hasLegacySettings = std::filesystem::exists(kLegacySettingsPath, error);
        if (error) {
            logger::error("[D2H Settings] Cannot check legacy settings path: {}", error.message());
            return;
        }
        if (!hasLegacySettings) {
            logger::info("[D2H Settings] No settings file found; using defaults until first change");
            return;
        }
        if (!ReadSettings(kLegacySettingsPath)) {
            logger::error("[D2H Settings] Legacy settings import failed; original file was left intact");
            return;
        }
        if (WriteSettings()) {
            logger::info("[D2H Settings] Migrated '{}' -> '{}'; legacy file was not modified",
                kLegacySettingsPath, kSettingsPath);
        } else {
            logger::error("[D2H Settings] Legacy settings loaded but migration save failed; legacy file is intact");
        }
    }

    bool HasRequiredPerk(RE::Actor* actor, RE::FormID perkID) {
        if (perkID == 0) {
            logger::debug("[D2H EquipDebug] HasRequiredPerk perkID=0 -> true");
            return true;
        }

        auto* perk = RE::TESForm::LookupByID<RE::BGSPerk>(perkID);
        const bool result = actor && perk && actor->HasPerk(perk);
        logger::debug(
            "[D2H EquipDebug] HasRequiredPerk actor={:08X} perkID={:08X} perkFound={} result={}",
            actor ? actor->GetFormID() : 0,
            perkID,
            static_cast<bool>(perk),
            result);
        return result;
    }

    void PlayerMenu() {
        bool changed = false;

        // Match the categorized, collapsible layout used by Parry for All.
        // Only UI presentation changes here; all settings retain their existing keys and behavior.
        if (ImGuiMCP::CollapsingHeader(GetLoc("menu.grip_requirements", "Grip Requirements"), ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGuiMCP::Indent();
            ImGuiMCP::Text("%s", GetLoc("menu.minimum_level", "Minimum Level:"));
            ImGuiMCP::SameLine();
            ImGuiMCP::SetNextItemWidth(120.0f);
            if (ImGuiMCP::InputInt("##PlayerLevelInput", &Settings.playerMinimumLevel, 0, 0)) {
                Settings.playerMinimumLevel = std::clamp(Settings.playerMinimumLevel, 0, 255);
                changed = true;
            }
            ImGuiMCP::TextWrapped("%s", GetLoc("menu.grip_level_note", "Required for both grip conversions."));
            ImGuiMCP::Unindent();
        }

        ImGuiMCP::Spacing();
        if (ImGuiMCP::CollapsingHeader(GetLoc("menu.two_h_as_one_h", "2H as 1H"), ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGuiMCP::Indent();
            if (ImGuiMCP::Checkbox(GetLoc("menu.enable_two_h_as_one_h", "Enable 2H as 1H"), &Settings.playerTwoHandedAsOneHanded)) changed = true;
            ImGuiMCP::BeginDisabled(!Settings.playerTwoHandedAsOneHanded);
            if (DrawPerkDropdown((std::string(GetLoc("menu.required_perk", "Required Perk")) + "##2HAs1H").c_str(), Settings.playerTwoHandedAsOneHandedPerk)) changed = true;
            ImGuiMCP::EndDisabled();
            ImGuiMCP::Unindent();
        }

        ImGuiMCP::Spacing();
        if (ImGuiMCP::CollapsingHeader(GetLoc("menu.dual_two_h", "Dual 2H"))) {
            ImGuiMCP::Indent();
            ImGuiMCP::Text("%s", GetLoc("menu.minimum_level", "Minimum Level:"));
            ImGuiMCP::SameLine();
            ImGuiMCP::SetNextItemWidth(120.0f);
            if (ImGuiMCP::InputInt("##PlayerDual2HLevel", &Settings.playerDualTwoHandedMinimumLevel, 0, 0)) {
                Settings.playerDualTwoHandedMinimumLevel = std::clamp(Settings.playerDualTwoHandedMinimumLevel, 0, 255);
                changed = true;
            }
            if (DrawPerkDropdown((std::string(GetLoc("menu.required_perk", "Required Perk")) + "##PlayerDual2H").c_str(), Settings.playerDualTwoHandedPerk)) changed = true;
            ImGuiMCP::TextWrapped("%s", GetLoc("menu.dual_note", "Also requires the 2H as 1H level and perk. Applies to API equip too."));
            ImGuiMCP::Unindent();
        }

        ImGuiMCP::Spacing();
        if (ImGuiMCP::CollapsingHeader(GetLoc("menu.one_h_as_two_h", "1H as 2H"))) {
            ImGuiMCP::Indent();
            if (ImGuiMCP::Checkbox(GetLoc("menu.enable_one_h_as_two_h", "Enable 1H as 2H"), &Settings.playerOneHandedAsTwoHanded)) changed = true;
            ImGuiMCP::BeginDisabled(!Settings.playerOneHandedAsTwoHanded);
            if (DrawPerkDropdown((std::string(GetLoc("menu.required_perk", "Required Perk")) + "##1HAs2H").c_str(), Settings.playerOneHandedAsTwoHandedPerk)) changed = true;
            ImGuiMCP::EndDisabled();
            ImGuiMCP::Unindent();
        }

        ImGuiMCP::Spacing();
        if (ImGuiMCP::CollapsingHeader(GetLoc("menu.normal_equip", "Normal Equip"), ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGuiMCP::Indent();
            ImGuiMCP::BeginDisabled(!Settings.playerTwoHandedAsOneHanded);
            if (ImGuiMCP::Checkbox(GetLoc("menu.normal_two_h_one_hand", "Equip 2H in one hand"), &Settings.playerNormalTwoHandedAsOneHanded)) changed = true;
            ImGuiMCP::EndDisabled();
            if (ImGuiMCP::Checkbox(GetLoc("menu.normal_second_equip", "Second equip switches to 2H"), &Settings.playerNormalOneHandedSecondEquipAsTwoHanded)) {
                changed = true;
            }
            ImGuiMCP::TextWrapped("%s", GetLoc("menu.normal_equip_note", "Normal Skyrim equip only; explicit API commands are unchanged."));
            ImGuiMCP::Unindent();
        }

        if (changed) SaveSe();
    }

    void ModMenu() {
        bool changed = false;

        if (ImGuiMCP::CollapsingHeader(GetLoc("menu.npc_dual_requirements", "Dual 2H Requirements"), ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGuiMCP::Indent();
            ImGuiMCP::Text("%s", GetLoc("menu.minimum_level", "Minimum Level:"));
            ImGuiMCP::SameLine();
            ImGuiMCP::SetNextItemWidth(120.0f);
            if (ImGuiMCP::InputInt("##LevelInput", &Settings.minimumLevel, 0, 0)) {
                Settings.minimumLevel = std::clamp(Settings.minimumLevel, 0, 255);
                changed = true;
            }

            ImGuiMCP::Text("%s", GetLoc("menu.minimum_two_h_skill", "Minimum 2H Skill:"));
            ImGuiMCP::SameLine();
            ImGuiMCP::SetNextItemWidth(120.0f);
            if (ImGuiMCP::InputFloat("##SkillInput", &Settings.skillValue, 0.0f, 0.0f, "%.0f")) {
                Settings.skillValue = std::clamp(Settings.skillValue, 0.0f, 100.0f);
                changed = true;
            }
            if (DrawPerkDropdown((std::string(GetLoc("menu.required_perk", "Required Perk")) + "##NPC").c_str(), Settings.npcRequiredPerk)) changed = true;
            ImGuiMCP::Unindent();
        }

        if (changed) SaveSe();
    }

    void Register() {
        if (SKSEMenuFramework::IsInstalled()) {
            LoadLanguage();
            // Define a se��o principal no menu do SKSE
            SKSEMenuFramework::SetSection(GetLoc("menu.section", "Dynamic Two-Handed"));

            // Adiciona o item que chama a fun��o ModMenu
            SKSEMenuFramework::AddSectionItem(GetLoc("menu.player_settings", "Player Settings"), PlayerMenu);
            SKSEMenuFramework::AddSectionItem(GetLoc("menu.npc_settings", "NPC Settings"), ModMenu);

            logger::info("Dynamic Two-Handed menu registered successfully.");
        }
    }
}
