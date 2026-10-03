#include "GripInput.h"

#include "Hooks.h"
#include "InputManagerAPI.h"
#include "Settings.h"

#include "SKSEMCP/SKSEMenuFramework.hpp"

#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <cmath>
#include <limits>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace GripInput {
    namespace ImGui = ImGuiMCP;
    namespace {
        constexpr auto kModName = "Dynamic Two-Handed";
        constexpr auto kSettingsPath = "Data/Viny Mods/Dynamic Two-Handed/InputSettings.json";
        constexpr std::uint32_t kMouseOffset = 256;
        constexpr std::uint32_t kGamepadOffset = 266;
        constexpr int kTap = 1;
        constexpr int kPress = 4;

        struct KeyOption {
            std::uint32_t id;
            const char* name;
        };

        enum class Mode { OneHanded, TwoHanded, Cycle };
        constexpr bool WantsTwoHanded(Mode mode, bool currentlyTwoHanded) {
            return mode == Mode::TwoHanded || (mode == Mode::Cycle && !currentlyTwoHanded);
        }
        static_assert(!WantsTwoHanded(Mode::OneHanded, true));
        static_assert(!WantsTwoHanded(Mode::OneHanded, false));
        static_assert(WantsTwoHanded(Mode::TwoHanded, false));
        static_assert(WantsTwoHanded(Mode::TwoHanded, true));
        static_assert(WantsTwoHanded(Mode::Cycle, false));
        static_assert(!WantsTwoHanded(Mode::Cycle, true));
        struct Binding {
            const char* purpose;
            const char* labelKey;
            const char* label;
            Mode mode;
            std::vector<int> actionIDs;
        };
        std::array bindings{
            Binding{ "ChangeTo1HGrip", "input.change_1h", "Change to 1h grip", Mode::OneHanded, {} },
            Binding{ "ChangeTo2HGrip", "input.change_2h", "Change to 2h grip", Mode::TwoHanded, {} },
            Binding{ "CycleGrip", "input.cycle", "Cycle grip", Mode::Cycle, {} }
        };
        bool settingsLoaded = false;

        std::string L(const char* key, const char* fallback) {
            return ModSettings::GetInputLoc(key, fallback);
        }

        std::string Lower(std::string text) {
            std::ranges::transform(text, text.begin(), [](unsigned char value) {
                return static_cast<char>(std::tolower(value));
            });
            return text;
        }

        const std::vector<KeyOption>& PCKeys() {
            using K = RE::BSKeyboardDevice::Keys;
            using M = RE::BSWin32MouseDevice::Keys;
            static const std::vector<KeyOption> keys{
                { 0, "None" },
                { M::kLeftButton + kMouseOffset, "Mouse 1 (Left)" },
                { M::kRightButton + kMouseOffset, "Mouse 2 (Right)" },
                { M::kMiddleButton + kMouseOffset, "Mouse 3 (Middle)" },
                { M::kButton3 + kMouseOffset, "Mouse 4" },
                { M::kButton4 + kMouseOffset, "Mouse 5" },
                { M::kButton5 + kMouseOffset, "Mouse 6" },
                { M::kButton6 + kMouseOffset, "Mouse 7" },
                { M::kButton7 + kMouseOffset, "Mouse 8" },
                { M::kWheelUp + kMouseOffset, "Mouse Wheel Up" },
                { M::kWheelDown + kMouseOffset, "Mouse Wheel Down" },
                { K::kA, "A" }, { K::kB, "B" }, { K::kC, "C" }, { K::kD, "D" },
                { K::kE, "E" }, { K::kF, "F" }, { K::kG, "G" }, { K::kH, "H" },
                { K::kI, "I" }, { K::kJ, "J" }, { K::kK, "K" }, { K::kL, "L" },
                { K::kM, "M" }, { K::kN, "N" }, { K::kO, "O" }, { K::kP, "P" },
                { K::kQ, "Q" }, { K::kR, "R" }, { K::kS, "S" }, { K::kT, "T" },
                { K::kU, "U" }, { K::kV, "V" }, { K::kW, "W" }, { K::kX, "X" },
                { K::kY, "Y" }, { K::kZ, "Z" },
                { K::kNum1, "1" }, { K::kNum2, "2" }, { K::kNum3, "3" }, { K::kNum4, "4" },
                { K::kNum5, "5" }, { K::kNum6, "6" }, { K::kNum7, "7" }, { K::kNum8, "8" },
                { K::kNum9, "9" }, { K::kNum0, "0" },
                { K::kMinus, "Minus (-)" }, { K::kEquals, "Equals (=)" },
                { K::kBracketLeft, "Bracket Left ([)" }, { K::kBracketRight, "Bracket Right (])" },
                { K::kSemicolon, "Semicolon (;)" }, { K::kApostrophe, "Apostrophe (')" },
                { K::kTilde, "Tilde (~)" }, { K::kBackslash, "Backslash" },
                { K::kComma, "Comma (,)" }, { K::kPeriod, "Period (.)" }, { K::kSlash, "Slash (/)" },
                { K::kF1, "F1" }, { K::kF2, "F2" }, { K::kF3, "F3" }, { K::kF4, "F4" },
                { K::kF5, "F5" }, { K::kF6, "F6" }, { K::kF7, "F7" }, { K::kF8, "F8" },
                { K::kF9, "F9" }, { K::kF10, "F10" }, { K::kF11, "F11" }, { K::kF12, "F12" },
                { K::kEscape, "Escape" }, { K::kTab, "Tab" }, { K::kSpacebar, "Space" },
                { K::kEnter, "Enter" }, { K::kBackspace, "Backspace" },
                { K::kLeftShift, "Shift (Left)" }, { K::kRightShift, "Shift (Right)" },
                { K::kLeftControl, "Ctrl (Left)" }, { K::kRightControl, "Ctrl (Right)" },
                { K::kLeftAlt, "Alt (Left)" }, { K::kRightAlt, "Alt (Right)" },
                { K::kPrintScreen, "Print Screen" }, { K::kScrollLock, "Scroll Lock" },
                { K::kPause, "Pause" }, { K::kNumLock, "Num Lock" },
                { K::kUp, "Up Arrow" }, { K::kDown, "Down Arrow" },
                { K::kLeft, "Left Arrow" }, { K::kRight, "Right Arrow" },
                { K::kInsert, "Insert" }, { K::kDelete, "Delete" }, { K::kHome, "Home" },
                { K::kEnd, "End" }, { K::kPageUp, "Page Up" }, { K::kPageDown, "Page Down" },
                { K::kKP_0, "Num 0" }, { K::kKP_1, "Num 1" }, { K::kKP_2, "Num 2" },
                { K::kKP_3, "Num 3" }, { K::kKP_4, "Num 4" }, { K::kKP_5, "Num 5" },
                { K::kKP_6, "Num 6" }, { K::kKP_7, "Num 7" }, { K::kKP_8, "Num 8" },
                { K::kKP_9, "Num 9" }, { K::kKP_Plus, "Num +" }, { K::kKP_Subtract, "Num -" },
                { K::kKP_Multiply, "Num *" }, { K::kKP_Divide, "Num /" },
                { K::kKP_Enter, "Num Enter" }, { K::kKP_Decimal, "Num Dot" }
            };
            return keys;
        }

        const std::vector<KeyOption>& GamepadKeys() {
            using K = RE::BSWin32GamepadDevice::Keys;
            static const std::vector<KeyOption> keys{
                { 0, "None" },
                { K::kUp + kGamepadOffset, "D-Pad Up" }, { K::kDown + kGamepadOffset, "D-Pad Down" },
                { K::kLeft + kGamepadOffset, "D-Pad Left" }, { K::kRight + kGamepadOffset, "D-Pad Right" },
                { K::kStart + kGamepadOffset, "Start / Options" },
                { K::kBack + kGamepadOffset, "Back / Share / Select" },
                { K::kLeftThumb + kGamepadOffset, "LS / L3" },
                { K::kRightThumb + kGamepadOffset, "RS / R3" },
                { K::kLeftShoulder + kGamepadOffset, "LB / L1" },
                { K::kRightShoulder + kGamepadOffset, "RB / R1" },
                { K::kLeftTrigger + kGamepadOffset, "LT / L2" },
                { K::kRightTrigger + kGamepadOffset, "RT / R2" },
                { K::kA + kGamepadOffset, "A / Cross" }, { K::kB + kGamepadOffset, "B / Circle" },
                { K::kX + kGamepadOffset, "X / Square" }, { K::kY + kGamepadOffset, "Y / Triangle" }
            };
            return keys;
        }

        const char* KeyName(std::uint32_t id, const std::vector<KeyOption>& keys) {
            const auto found = std::ranges::find(keys, id, &KeyOption::id);
            return found == keys.end() ? "Unknown" : found->name;
        }

        std::string ActionName(int action) {
            switch (action) {
            case 0: return L("input.ignore", "Ignore");
            case kTap: return L("input.tap", "Tap");
            case 2: return L("input.hold", "Hold");
            case 3: return L("input.gesture", "Gesture");
            case kPress: return L("input.press", "Press");
            default: return L("input.unknown", "Unknown");
            }
        }

        bool IsValid(int id) {
            auto* api = InputManagerAPI::_APIV2;
            return api && id >= 0 && static_cast<std::size_t>(id) < api->GetInputCount(0) &&
                api->GetActionInfo(id).isValid;
        }

        void InputTooltip(int id) {
            if (!ImGui::IsItemHovered(ImGui::ImGuiHoveredFlags_AllowWhenDisabled) || !IsValid(id)) return;
            auto* api = InputManagerAPI::_APIV2;
            const auto info = api->GetActionInfo(id);
            const auto actionName = [](int action, int taps) {
                auto name = ActionName(action);
                if (action == kTap) name += " x" + std::to_string(taps);
                return name;
            };
            const auto keyName = [](std::uint32_t key, const auto& keys) -> std::string {
                switch (key) {
                case InputManagerAPI::VKEY_DIR_UP: return L("input.up", "Up");
                case InputManagerAPI::VKEY_DIR_DOWN: return L("input.down", "Down");
                case InputManagerAPI::VKEY_DIR_LEFT: return L("input.left", "Left");
                case InputManagerAPI::VKEY_DIR_RIGHT: return L("input.right", "Right");
                case InputManagerAPI::VKEY_DIR_UPRIGHT: return L("input.up_right", "Up-Right");
                case InputManagerAPI::VKEY_DIR_UPLEFT: return L("input.up_left", "Up-Left");
                case InputManagerAPI::VKEY_DIR_DOWNRIGHT: return L("input.down_right", "Down-Right");
                case InputManagerAPI::VKEY_DIR_DOWNLEFT: return L("input.down_left", "Down-Left");
                default: return key == 0 ? L("input.none", "None") : KeyName(key, keys);
                }
            };
            const auto modifierName = [&](std::uint32_t key, int action, const auto& keys) -> std::string {
                if (action != 3) return keyName(key, keys);
                const auto* name = key < api->GetInputCount(2) ? api->GetInputName(2, static_cast<int>(key)) : nullptr;
                return name ? name : L("input.unknown", "Unknown");
            };
            const auto show = [&](const char* label, const std::string& key, int action, int taps) {
                ImGui::Text("%s: %s (%s)", label, key.c_str(), actionName(action, taps).c_str());
            };
            ImGui::BeginTooltip();
            ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "%s", L("input.details", "Input Details").c_str());
            ImGui::Separator();
            ImGui::Text("[%d] %s", info.id, info.name ? info.name : "Unknown");
            show(L("input.pc_main_key", "PC Main Key").c_str(), keyName(info.pcMainKey, PCKeys()), info.pcMainAction, info.pcMainTapCount);
            if (info.pcModifierKey != 0 || info.pcModAction == 3) {
                show(L("input.pc_modifier_key", "PC Modifier Key").c_str(), modifierName(info.pcModifierKey, info.pcModAction, PCKeys()), info.pcModAction, info.pcModTapCount);
            }
            show(L("input.gamepad_main_key", "Gamepad Main Key").c_str(), keyName(info.gamepadMainKey, GamepadKeys()), info.gamepadMainAction, info.gamepadMainTapCount);
            if (info.gamepadModifierKey != 0 || info.gamepadModAction == 3) {
                show(L("input.gamepad_modifier_key", "Gamepad Modifier Key").c_str(), modifierName(info.gamepadModifierKey, info.gamepadModAction, GamepadKeys()), info.gamepadModAction, info.gamepadModTapCount);
                if (info.gamepadModAction == 3) {
                    ImGui::Text("%s: %s", L("input.gesture_stick", "Gesture Stick").c_str(),
                        info.gamepadGestureStick == 0 ? L("input.left_stick", "Left Stick").c_str() : L("input.right_stick", "Right Stick").c_str());
                }
            }
            if (info.useCustomTimings) {
                ImGui::TextColored({ 0.8f, 0.8f, 0.4f, 1.0f }, "%s - %s: %.2fs | %s: %.2fs",
                    L("input.custom_timings", "Custom Timings").c_str(), L("input.tap_window", "Tap Window").c_str(),
                    info.tapWindow, L("input.hold_duration", "Hold Duration").c_str(), info.holdDuration);
            }
            ImGui::EndTooltip();
        }

        void Save() {
            std::error_code error;
            const std::filesystem::path path(kSettingsPath);
            std::filesystem::create_directories(path.parent_path(), error);
            if (error) return;
            rapidjson::Document document;
            document.SetObject();
            auto& allocator = document.GetAllocator();
            for (const auto& binding : bindings) {
                rapidjson::Value ids(rapidjson::kArrayType);
                for (const auto id : binding.actionIDs) ids.PushBack(id, allocator);
                document.AddMember(rapidjson::Value(binding.purpose, allocator), ids, allocator);
            }
            rapidjson::StringBuffer buffer;
            rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
            document.Accept(writer);
            std::ofstream output(path);
            output << buffer.GetString();
        }

        void Load() {
            if (settingsLoaded) return;
            settingsLoaded = true;
            std::ifstream input(kSettingsPath);
            if (!input) return;
            const std::string text(std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{});
            rapidjson::Document document;
            document.Parse(text.c_str());
            if (document.HasParseError() || !document.IsObject()) return;
            for (auto& binding : bindings) {
                if (!document.HasMember(binding.purpose) || !document[binding.purpose].IsArray()) continue;
                for (const auto& value : document[binding.purpose].GetArray()) {
                    if (!value.IsInt() || value.GetInt() < 0) continue;
                    const auto id = value.GetInt();
                    const bool assigned = std::ranges::any_of(bindings, [id](const auto& item) {
                        return std::ranges::contains(item.actionIDs, id);
                    });
                    if (!assigned) binding.actionIDs.push_back(id);
                }
            }
        }

        void Listen(const Binding& binding, int id, bool enabled) {
            if (IsValid(id)) InputManagerAPI::_APIV2->UpdateListener(0, id, kModName, binding.purpose, enabled);
        }

        void Reconcile() {
            // As in Menu-Popup, wait for Input Manager to load its action table
            // before pruning saved IDs (the API may arrive earlier).
            if (!IsAvailable() || InputManagerAPI::_APIV2->GetInputCount(0) == 0) return;
            for (auto& binding : bindings) {
                for (const auto id : binding.actionIDs) Listen(binding, id, false);
                std::erase_if(binding.actionIDs, [](int id) { return !IsValid(id); });
                for (const auto id : binding.actionIDs) Listen(binding, id, true);
            }
            Save();
        }

        bool KeyCombo(const char* label, std::uint32_t& current, const std::vector<KeyOption>& keys) {
            bool changed = false;
            if (ImGui::BeginCombo(label, KeyName(current, keys))) {
                static std::unordered_map<std::string, std::string> searches;
                auto& search = searches[label];
                char buffer[128]{};
                strncpy_s(buffer, search.c_str(), _TRUNCATE);
                if (ImGui::IsWindowAppearing()) {
                    buffer[0] = '\0';
                    search.clear();
                    ImGui::SetKeyboardFocusHere();
                }
                if (ImGui::InputText((L("input.filter", "Filter...") + "##" + label).c_str(), buffer, sizeof(buffer))) {
                    search = buffer;
                }
                ImGui::Separator();
                const auto filter = Lower(search);
                for (const auto& key : keys) {
                    if (!filter.empty() && Lower(key.name).find(filter) == std::string::npos) continue;
                    if (ImGui::Selectable(key.name, key.id == current)) {
                        current = key.id;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        void ActionCombo(const char* label, int& action, bool modifier, int mainAction) {
            action = std::clamp(action, 0, 4);
            const auto preview = ActionName(action);
            if (!ImGui::BeginCombo(label, preview.c_str())) return;
            for (int value = 0; value <= 4; ++value) {
                if (!modifier && value == 3) continue;
                if (modifier && value == 3 && mainAction != 2 && mainAction != 4) continue;
                const auto name = ActionName(value);
                if (ImGui::Selectable(name.c_str(), value == action)) action = value;
            }
            ImGui::EndCombo();
        }

        void GestureCombo(const char* label, std::uint32_t& gesture) {
            auto* api = InputManagerAPI::_APIV2;
            const auto count = api->GetInputCount(2);
            const auto current = static_cast<std::size_t>(gesture);
            const auto* preview = current < count ? api->GetInputName(2, static_cast<int>(current)) : nullptr;
            const auto fallback = L("input.none", "None");
            if (!ImGui::BeginCombo(label, preview ? preview : fallback.c_str())) return;
            for (std::size_t index = 0; index < count; ++index) {
                const auto* name = api->GetInputName(2, static_cast<int>(index));
                if (ImGui::Selectable(name ? name : fallback.c_str(), index == current)) {
                    gesture = static_cast<std::uint32_t>(index);
                }
            }
            ImGui::EndCombo();
        }

        void StickCombo(const char* label, int& stick) {
            const std::array names{ L("input.left_stick", "Left Stick"), L("input.right_stick", "Right Stick") };
            stick = std::clamp(stick, 0, 1);
            if (!ImGui::BeginCombo(label, names[static_cast<std::size_t>(stick)].c_str())) return;
            for (int index = 0; index < 2; ++index) {
                if (ImGui::Selectable(names[static_cast<std::size_t>(index)].c_str(), index == stick)) stick = index;
            }
            ImGui::EndCombo();
        }

        std::string UpdateError(const InputManagerAPI::ActionUpdateResultV2& result) {
            if (result.message[0]) return result.message;
            return L("input.invalid_or_conflict", "Conflict detected or invalid input.");
        }

        void ActionEditor(const char* popup, int& editingID, InputManagerAPI::ActionInfo& mapping,
            std::string& error, bool creating = false, char* name = nullptr, int* createdID = nullptr) {
            if (!ImGui::BeginPopup(popup)) return;
            if (creating) ImGui::InputText(L("input.action_name", "Action name").c_str(), name, 128);
            ImGui::Text("%s", L("input.pc_settings", "PC Settings").c_str());
            KeyCombo(L("input.pc_main_key", "PC Main Key").c_str(), mapping.pcMainKey, PCKeys());
            ActionCombo(L("input.pc_main_action", "PC Main Action").c_str(), mapping.pcMainAction, false, mapping.pcMainAction);
            if (mapping.pcMainAction == kTap) ImGui::InputInt(L("input.pc_main_taps", "PC Main Taps").c_str(), &mapping.pcMainTapCount);
            if (mapping.pcModAction == 3 && mapping.pcMainAction != 2 && mapping.pcMainAction != kPress) {
                mapping.pcModAction = 0;
            }
            ActionCombo(L("input.pc_modifier_action", "PC Modifier Action").c_str(), mapping.pcModAction, true, mapping.pcMainAction);
            if (mapping.pcModAction == 3) {
                GestureCombo(L("input.pc_gesture", "PC Gesture").c_str(), mapping.pcModifierKey);
            } else {
                KeyCombo(L("input.pc_modifier_key", "PC Modifier Key").c_str(), mapping.pcModifierKey, PCKeys());
                if (mapping.pcModAction == kTap) ImGui::InputInt(L("input.pc_modifier_taps", "PC Modifier Taps").c_str(), &mapping.pcModTapCount);
            }

            ImGui::Spacing();
            ImGui::Text("%s", L("input.gamepad_settings", "Gamepad Settings").c_str());
            KeyCombo(L("input.gamepad_main_key", "Gamepad Main Key").c_str(), mapping.gamepadMainKey, GamepadKeys());
            ActionCombo(L("input.gamepad_main_action", "Gamepad Main Action").c_str(), mapping.gamepadMainAction, false, mapping.gamepadMainAction);
            if (mapping.gamepadMainAction == kTap) ImGui::InputInt(L("input.gamepad_main_taps", "Gamepad Main Taps").c_str(), &mapping.gamepadMainTapCount);
            if (mapping.gamepadModAction == 3 && mapping.gamepadMainAction != 2 && mapping.gamepadMainAction != kPress) {
                mapping.gamepadModAction = 0;
            }
            ActionCombo(L("input.gamepad_modifier_action", "Gamepad Modifier Action").c_str(), mapping.gamepadModAction, true, mapping.gamepadMainAction);
            if (mapping.gamepadModAction == 3) {
                GestureCombo(L("input.gamepad_gesture", "Gamepad Gesture").c_str(), mapping.gamepadModifierKey);
                StickCombo(L("input.gesture_stick", "Gesture Stick").c_str(), mapping.gamepadGestureStick);
            } else {
                KeyCombo(L("input.gamepad_modifier_key", "Gamepad Modifier Key").c_str(), mapping.gamepadModifierKey, GamepadKeys());
                if (mapping.gamepadModAction == kTap) ImGui::InputInt(L("input.gamepad_modifier_taps", "Gamepad Modifier Taps").c_str(), &mapping.gamepadModTapCount);
            }

            mapping.pcMainTapCount = std::max(mapping.pcMainTapCount, 1);
            mapping.pcModTapCount = std::max(mapping.pcModTapCount, 1);
            mapping.gamepadMainTapCount = std::max(mapping.gamepadMainTapCount, 1);
            mapping.gamepadModTapCount = std::max(mapping.gamepadModTapCount, 1);
            if (!error.empty()) ImGui::TextColored({ 1.0f, 0.2f, 0.2f, 1.0f }, "%s", error.c_str());
            if (ImGui::Button(L("input.save", "Save").c_str(), { 120.0f, 0.0f })) {
                if (creating && (!name || !name[0])) {
                    error = L("input.name_required", "Action name is required.");
                } else {
                    int id = editingID;
                    if (creating) {
                        id = InputManagerAPI::_APIV2->CreateInput(0, name);
                        mapping.id = id;
                        mapping.name = id >= 0 ? InputManagerAPI::_APIV2->GetInputName(0, id) : nullptr;
                        mapping.isValid = id >= 0;
                    }
                    if (id < 0) {
                        error = L("input.create_failed", "Could not create Action.");
                    } else {
                        const auto result = InputManagerAPI::_APIV2->UpdateActionMappingV2(id, mapping);
                        if (result.success && result.code == InputManagerAPI::ActionUpdateCode::kSuccess) {
                            if (createdID) *createdID = id;
                            editingID = -1;
                            error.clear();
                            ImGui::CloseCurrentPopup();
                        } else {
                            error = UpdateError(result);
                            if (creating) InputManagerAPI::_APIV2->DeleteInput(0, id);
                        }
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(L("input.cancel", "Cancel").c_str(), { 120.0f, 0.0f })) {
                editingID = -1;
                error.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        void AddInputUI(Binding& binding) {
            auto& actionIDs = binding.actionIDs;
            auto* api = InputManagerAPI::_APIV2;
            static int editingID = -1;
            static InputManagerAPI::ActionInfo editing{};
            static std::string editError;
            static InputManagerAPI::ActionInfo creating{};
            static std::string createError;
            static char createName[128]{};

            bool openEdit = false;
            for (std::size_t index = 0; index < actionIDs.size();) {
                const auto id = actionIDs[index];
                const auto* name = IsValid(id) ? api->GetInputName(0, id) : nullptr;
                ImGui::PushID(id);
                ImGui::Text("[%d] %s", id, name ? name : "Unknown");
                InputTooltip(id);
                ImGui::SameLine();
                if (ImGui::Button(L("input.edit", "Edit").c_str()) && IsValid(id)) {
                    editingID = id;
                    editing = api->GetActionInfo(id);
                    editError.clear();
                    openEdit = true;
                }
                ImGui::SameLine();
                if (ImGui::Button(L("input.remove", "Remove").c_str())) {
                    Listen(binding, id, false);
                    actionIDs.erase(actionIDs.begin() + index);
                    Save();
                    ImGui::PopID();
                    continue;
                }
                ImGui::PopID();
                ++index;
            }
            if (openEdit) ImGui::OpenPopup("EditPopupInput");
            ActionEditor("EditPopupInput", editingID, editing, editError);

            if (ImGui::Button(L("input.add", "+ Add Input").c_str())) ImGui::OpenPopup("AddPopupInput");
            bool openCreate = false;
            if (ImGui::BeginPopup("AddPopupInput")) {
                if (ImGui::Button(L("input.create", "Create").c_str(), { 140.0f, 0.0f })) {
                    createName[0] = '\0';
                    creating = {};
                    creating.id = -1;
                    creating.pcMainTapCount = creating.pcModTapCount = 1;
                    creating.gamepadMainTapCount = creating.gamepadModTapCount = 1;
                    creating.holdDuration = 0.30f;
                    creating.tapWindow = 0.25f;
                    creating.isValid = true;
                    createError.clear();
                    openCreate = true;
                    ImGui::CloseCurrentPopup();
                }
                static char filterBuffer[128]{};
                ImGui::InputText(L("input.filter", "Filter...").c_str(), filterBuffer, sizeof(filterBuffer));
                const auto filter = Lower(filterBuffer);
                ImGui::BeginChild("PopupInputList", { 360.0f, 230.0f }, true);
                const auto count = api->GetInputCount(0);
                for (std::size_t index = 0; index < count; ++index) {
                    const auto id = static_cast<int>(index);
                    if (!IsValid(id)) continue;
                    const auto* name = api->GetInputName(0, id);
                    const auto label = "[" + std::to_string(id) + "] " + (name ? name : "Unknown");
                    if (!filter.empty() && Lower(label).find(filter) == std::string::npos) continue;
                    const bool assigned = std::ranges::any_of(bindings, [id](const auto& item) {
                        return std::ranges::contains(item.actionIDs, id);
                    });
                    ImGui::BeginDisabled(assigned);
                    const bool selected = ImGui::Selectable(label.c_str(), false);
                    ImGui::EndDisabled();
                    InputTooltip(id);
                    if (selected && !assigned) {
                        actionIDs.push_back(id);
                        Listen(binding, id, true);
                        Save();
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::EndChild();
                ImGui::EndPopup();
            }
            if (openCreate) ImGui::OpenPopup("CreatePopupInput");
            int createdID = -1;
            int createEditingID = -1;
            ActionEditor("CreatePopupInput", createEditingID, creating, createError, true, createName, &createdID);
            if (createdID >= 0 && !std::ranges::contains(actionIDs, createdID)) {
                actionIDs.push_back(createdID);
                Listen(binding, createdID, true);
                Save();
            }
        }

        void ApplyGrip(Mode mode) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* ui = RE::UI::GetSingleton();
            if (!player || !ui || ui->GameIsPaused() || player->IsDead() ||
                !Hooks::g_twoHandSlot || !Hooks::g_rightHandSlot || !Hooks::g_leftHandSlot) return;
            using Grip = DYNAMIC_TWO_HANDED_API::Grip;
            using Hand = DYNAMIC_TWO_HANDED_API::Hand;
            const auto weaponIn = [player](RE::BGSEquipSlot* slot) -> RE::TESObjectWEAP* {
                auto* form = player->GetEquippedObjectInSlot(slot);
                return form && (Hooks::isOneHanded(form) || Hooks::isTwoHanded(form))
                    ? form->As<RE::TESObjectWEAP>() : nullptr;
            };
            auto* weapon = weaponIn(Hooks::g_twoHandSlot);
            const bool currentlyTwoHanded = weapon != nullptr;
            const bool wantsTwoHanded = WantsTwoHanded(mode, currentlyTwoHanded);
            // Fixed grip actions are idempotent: pressing 1H/2H again does not unequip.
            if (wantsTwoHanded == currentlyTwoHanded) return;
            bool left = false;
            if (!weapon) {
                weapon = weaponIn(Hooks::g_rightHandSlot);
                if (!weapon) {
                    weapon = weaponIn(Hooks::g_leftHandSlot);
                    left = true;
                }
            }
            if (!weapon) return;
            const auto grip = wantsTwoHanded ? Grip::kTwoHanded : Grip::kOneHanded;
            const auto hand = wantsTwoHanded ? Hand::kBoth : Hand::kRight;
            if (!Hooks::CanEquipWithGrip(player, weapon, grip, hand)) return;
            RE::ExtraDataList* extra = nullptr;
            if (auto* entry = player->GetEquippedEntryData(left);
                entry && entry->object == weapon && entry->extraLists) {
                for (auto* candidate : *entry->extraLists) {
                    if (candidate && (left ? candidate->HasType<RE::ExtraWornLeft>() :
                        candidate->HasType<RE::ExtraWorn>())) {
                        extra = candidate;
                        break;
                    }
                }
            }
            // Remove the actual TwoHand instance before returning it to one hand,
            // including when the inventory contains other copies of the same form.
            if (currentlyTwoHanded && !Hooks::UnequipWithGrip(player, weapon, extra,
                Grip::kTwoHanded, Hand::kBoth)) return;
            Hooks::EquipWithGrip(player, weapon, extra, grip, hand);
        }

        void Initialize() {
            if (!IsAvailable()) return;
            Load();
            Reconcile();
        }

        class Listener final : public RE::BSTEventSink<SKSE::ModCallbackEvent> {
        public:
            RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* event,
                RE::BSTEventSource<SKSE::ModCallbackEvent>*) override {
                if (!event) return RE::BSEventNotifyControl::kContinue;
                const std::string_view name = event->eventName.c_str();
                if (name == "TweenPauseReady") {
                    InputManagerAPI::RequestAPIDirectV2();
                    Initialize();
                } else if (name == "InputManager_ActionTriggered" && IsAvailable() &&
                    std::isfinite(event->numArg) && event->numArg >= 0 &&
                    static_cast<double>(event->numArg) <= std::numeric_limits<int>::max()) {
                    const auto id = static_cast<int>(event->numArg);
                    if (event->numArg != static_cast<float>(id) || !IsValid(id)) return RE::BSEventNotifyControl::kContinue;
                    for (const auto& binding : bindings) {
                        if (!std::ranges::contains(binding.actionIDs, id)) continue;
                        if (auto* tasks = SKSE::GetTaskInterface()) {
                            tasks->AddTask([mode = binding.mode] { ApplyGrip(mode); });
                        }
                        break;
                    }
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        } listener;
    }

    bool IsAvailable() {
        return InputManagerAPI::_APIV2 &&
            InputManagerAPI::_APIV2->GetAPIVersion() >= InputManagerAPI::kAPIVersion2;
    }

    void RegisterListener() {
        if (auto* source = SKSE::GetModCallbackEventSource()) source->AddEventSink(std::addressof(listener));
    }

    void HandleMessage(SKSE::MessagingInterface::Message* message) {
        if (!message) return;
        if (message->type == InputManagerAPI::kMessage_ProvideAPIV2) InputManagerAPI::ReceiveAPI(message);
        if (message->type == SKSE::MessagingInterface::kPostLoad ||
            message->type == SKSE::MessagingInterface::kDataLoaded) {
            InputManagerAPI::RequestAPIDirectV2();
            if (!InputManagerAPI::_APIV2) InputManagerAPI::RequestAPIV2();
        }
        Initialize();
    }

    void DrawMenu() {
        if (!IsAvailable()) return;
        ImGui::TextWrapped("%s", L("input.help", "Choose Input Manager Actions for each grip command.").c_str());
        ImGui::TextWrapped("%s", L("input.weapon_priority", "Uses the right-hand weapon, or the left-hand weapon if the right has no supported weapon. Returning to 1H uses the right hand.").c_str());
        for (auto& binding : bindings) {
            ImGui::PushID(binding.purpose);
            if (ImGui::CollapsingHeader(L(binding.labelKey, binding.label).c_str())) AddInputUI(binding);
            ImGui::PopID();
        }
    }
}
