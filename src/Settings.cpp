#include "Settings.h"
#include <filesystem>

// Caminho onde o arquivo de configuração será salvo
const char* ConfigPath = "Data/SKSE/Plugins/Dynamic Two-Handed.json";

namespace ModSettings {

    void SaveSe() {
        rapidjson::Document doc;
        doc.SetObject();
        auto& allocator = doc.GetAllocator();

        // Adiciona os membros baseados na struct THConfig
        doc.AddMember("minimumLevel", Settings.minimumLevel, allocator);
        doc.AddMember("skillValue", Settings.skillValue, allocator);

        // Cria o diretório se não existir
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

                if (doc.HasMember("skillValue") && doc["skillValue"].IsFloat())
                    Settings.skillValue = doc["skillValue"].GetFloat();
            }
        }
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

            // Configuração da Skill de Duas Mãos
            ImGuiMCP::Text("Minimum 2H Skill:");
            ImGuiMCP::SameLine();
            ImGuiMCP::SetNextItemWidth(120.0f);
            // "%.0f" remove a exibição de casas decimais no InputFloat
            if (ImGuiMCP::InputFloat("##SkillInput", &Settings.skillValue, 0.0f, 0.0f, "%.0f")) {
                Settings.skillValue = std::clamp(Settings.skillValue, 0.0f, 100.0f);
                changed = true;
            }

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
            // Define a seção principal no menu do SKSE
            SKSEMenuFramework::SetSection("Dynamic Two-Handed");

            // Adiciona o item que chama a função ModMenu
            SKSEMenuFramework::AddSectionItem("General Settings", ModMenu);

            logger::info("Dynamic Two-Handed menu registered successfully.");
        }
    }
}