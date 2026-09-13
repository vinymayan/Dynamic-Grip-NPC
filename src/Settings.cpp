#include "Settings.h"
#include "Manager.h"
#include <filesystem>

// Caminho onde o arquivo de configura��o ser� salvo
const char* ConfigPath = "Data/SKSE/Plugins/Dynamic Two-Handed.json";

namespace ModSettings {

    namespace {
        bool DrawPerkDropdown(const char* label, RE::FormID& selectedPerk) {
            const auto& perks = Manager::GetSingleton()->GetPerks();
            std::vector<const char*> names{ "None" };
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

    void SaveSe() {
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
        doc.AddMember("playerNormalTwoHandedAsOneHanded", Settings.playerNormalTwoHandedAsOneHanded, allocator);
        doc.AddMember("playerNormalTwoHandedHand", Settings.playerNormalTwoHandedHand, allocator);
        doc.AddMember("playerOneHandedAsTwoHanded", Settings.playerOneHandedAsTwoHanded, allocator);
        AddFormID(doc, "playerOneHandedAsTwoHandedPerk", Settings.playerOneHandedAsTwoHandedPerk);

        // Cria o diret�rio se n�o existir
        std::filesystem::path path(ConfigPath);
        std::filesystem::create_directories(path.parent_path());

        FILE* fp = nullptr;
        fopen_s(&fp, ConfigPath, "wb");
        if (fp) {
            char writeBuffer[65536];
            rapidjson::FileWriteStream os(fp, writeBuffer, sizeof(writeBuffer));
            rapidjson::Writer<rapidjson::FileWriteStream> writer(os);
            doc.Accept(writer);
            fclose(fp);
        }
    }

    void LoadSE() {
        FILE* fp = nullptr;
        fopen_s(&fp, ConfigPath, "rb");
        if (fp) {
            char readBuffer[65536];
            rapidjson::FileReadStream is(fp, readBuffer, sizeof(readBuffer));
            rapidjson::Document doc;
            doc.ParseStream(is);
            fclose(fp);

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
                if (doc.HasMember("playerNormalTwoHandedAsOneHanded") && doc["playerNormalTwoHandedAsOneHanded"].IsBool())
                    Settings.playerNormalTwoHandedAsOneHanded = doc["playerNormalTwoHandedAsOneHanded"].GetBool();
                if (doc.HasMember("playerNormalTwoHandedHand") && doc["playerNormalTwoHandedHand"].IsInt())
                    Settings.playerNormalTwoHandedHand = std::clamp(doc["playerNormalTwoHandedHand"].GetInt(), 0, 1);
                if (doc.HasMember("playerOneHandedAsTwoHanded") && doc["playerOneHandedAsTwoHanded"].IsBool())
                    Settings.playerOneHandedAsTwoHanded = doc["playerOneHandedAsTwoHanded"].GetBool();
                ReadFormID(doc, "playerOneHandedAsTwoHandedPerk", Settings.playerOneHandedAsTwoHandedPerk);
            }
        }
    }

    bool HasRequiredPerk(RE::Actor* actor, RE::FormID perkID) {
        if (perkID == 0) return true;
        auto* perk = RE::TESForm::LookupByID<RE::BGSPerk>(perkID);
        return actor && perk && actor->HasPerk(perk);
    }

    void PlayerMenu() {
        bool changed = false;
        ImGuiMCP::TextColored({ 1.0f, 0.8f, 0.0f, 1.0f }, "Player Grip Settings");
        ImGuiMCP::Separator();
        ImGuiMCP::Spacing();

        ImGuiMCP::Text("Minimum Level:");
        ImGuiMCP::SameLine();
        ImGuiMCP::SetNextItemWidth(120.0f);
        if (ImGuiMCP::InputInt("##PlayerLevelInput", &Settings.playerMinimumLevel, 0, 0)) {
            Settings.playerMinimumLevel = std::clamp(Settings.playerMinimumLevel, 0, 255);
            changed = true;
        }

        ImGuiMCP::Spacing();
        if (ImGuiMCP::Checkbox("Enable 2H weapons as 1H", &Settings.playerTwoHandedAsOneHanded)) changed = true;
        ImGuiMCP::BeginDisabled(!Settings.playerTwoHandedAsOneHanded);
        if (DrawPerkDropdown("Required perk##2HAs1H", Settings.playerTwoHandedAsOneHandedPerk)) changed = true;
        if (ImGuiMCP::Checkbox("Normal 2H equip uses a one-hand slot", &Settings.playerNormalTwoHandedAsOneHanded)) changed = true;
        ImGuiMCP::BeginDisabled(!Settings.playerNormalTwoHandedAsOneHanded);
        const char* hands[] = { "Right", "Left" };
        ImGuiMCP::SetNextItemWidth(160.0f);
        if (ImGuiMCP::Combo("Normal equip hand", &Settings.playerNormalTwoHandedHand, hands, 2)) changed = true;
        ImGuiMCP::EndDisabled();
        ImGuiMCP::EndDisabled();

        ImGuiMCP::Spacing();
        if (ImGuiMCP::Checkbox("Enable 1H weapons as 2H", &Settings.playerOneHandedAsTwoHanded)) changed = true;
        ImGuiMCP::BeginDisabled(!Settings.playerOneHandedAsTwoHanded);
        if (DrawPerkDropdown("Required perk##1HAs2H", Settings.playerOneHandedAsTwoHandedPerk)) changed = true;
        ImGuiMCP::EndDisabled();

        if (changed) SaveSe();
    }

    void ModMenu() {
        bool changed = false;

        ImGuiMCP::TextColored({ 1.0f, 0.8f, 0.0f, 1.0f }, "NPC Requirements Settings");
        ImGuiMCP::Separator();
        ImGuiMCP::Spacing();

        if (ImGuiMCP::CollapsingHeader("Requirement Logic", ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGuiMCP::Indent();

            ImGuiMCP::Text("Minimum Level:");
            ImGuiMCP::SameLine();
            ImGuiMCP::SetNextItemWidth(120.0f);
            if (ImGuiMCP::InputInt("##LevelInput", &Settings.minimumLevel, 0, 0)) {
                Settings.minimumLevel = std::clamp(Settings.minimumLevel, 0, 255);
                changed = true;
            }

            ImGuiMCP::Spacing();

            // Configura��o da Skill de Duas M�os
            ImGuiMCP::Text("Minimum 2H Skill:");
            ImGuiMCP::SameLine();
            ImGuiMCP::SetNextItemWidth(120.0f);
            // "%.0f" remove a exibi��o de casas decimais no InputFloat
            if (ImGuiMCP::InputFloat("##SkillInput", &Settings.skillValue, 0.0f, 0.0f, "%.0f")) {
                Settings.skillValue = std::clamp(Settings.skillValue, 0.0f, 100.0f);
                changed = true;
            }

            ImGuiMCP::Spacing();
            if (DrawPerkDropdown("Required perk##NPC", Settings.npcRequiredPerk)) changed = true;

            ImGuiMCP::Unindent();
        }

        /*if (ImGuiMCP::CollapsingHeader("Information", ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGuiMCP::TextWrapped("These settings define the requirements for an NPC to be allowed to dual-wield two-handed weapons.");
            ImGuiMCP::BulletText("Current Level Required: %d", Settings.minimumLevel);
            ImGuiMCP::BulletText("Current Skill Required: %.1f", Settings.skillValue);
        }*/

        // Salva automaticamente se algo mudar
        if (changed) {
            SaveSe();
        }
    }

    void Register() {
        if (SKSEMenuFramework::IsInstalled()) {
            // Define a se��o principal no menu do SKSE
            SKSEMenuFramework::SetSection("Dynamic Two-Handed");

            // Adiciona o item que chama a fun��o ModMenu
            SKSEMenuFramework::AddSectionItem("General Settings", ModMenu);
            SKSEMenuFramework::AddSectionItem("Player Settings", PlayerMenu);

            logger::info("Dynamic Two-Handed menu registered successfully.");
        }
    }
}
