#include "Manager.h"

#include <algorithm>
#include <format>

namespace {
    const RE::TESFile* GetMasterFile(RE::TESForm* form) {
        if (!form) return nullptr;
        const auto id = form->GetFormID();
        auto* data = RE::TESDataHandler::GetSingleton();
        if (!data) return nullptr;
        if ((id >> 24) == 0xFE) {
            return data->LookupLoadedLightModByIndex(static_cast<std::uint16_t>((id >> 12) & 0xFFF));
        }
        return data->LookupLoadedModByIndex(static_cast<std::uint8_t>(id >> 24));
    }

    bool Includes(std::string_view list, std::string_view wanted) {
        for (std::size_t begin = 0; begin <= list.size();) {
            const auto end = list.find(',', begin);
            auto token = list.substr(begin, end == std::string_view::npos ? list.size() - begin : end - begin);
            while (!token.empty() && token.front() == ' ') token.remove_prefix(1);
            while (!token.empty() && token.back() == ' ') token.remove_suffix(1);
            if (token == wanted) return true;
            if (end == std::string_view::npos) break;
            begin = end + 1;
        }
        return false;
    }
}

std::string FormUtil::NormalizeFormID(RE::TESForm* form) {
    if (!form) return {};
    const auto id = form->GetFormID();
    const auto index = static_cast<std::uint8_t>(id >> 24);
    if (index == 0xFF) return std::format("{:X}", id);
    const auto* file = GetMasterFile(form);
    if (!file) return std::format("{:X}", id);
    const auto localID = index == 0xFE ? id & 0xFFF : id & 0x00FFFFFF;
    return std::format("{}|{:X}", file->GetFilename(), localID);
}

RE::FormID FormUtil::FormIDFromString(const std::string& value) {
    if (value.empty()) return 0;
    if (auto* form = RE::TESForm::LookupByEditorID(value)) return form->GetFormID();
    const auto separator = value.find('|');
    try {
        if (separator != std::string::npos) {
            auto* data = RE::TESDataHandler::GetSingleton();
            return data ? data->LookupFormID(
                static_cast<RE::FormID>(std::stoul(value.substr(separator + 1), nullptr, 16)),
                value.substr(0, separator)) : 0;
        }
        return static_cast<RE::FormID>(std::stoul(value, nullptr, 16));
    } catch (...) {
        return 0;
    }
}

Manager* Manager::GetSingleton() {
    static Manager singleton;
    return std::addressof(singleton);
}

void Manager::PopulateAllLists() {
    perks.clear();
    auto* data = RE::TESDataHandler::GetSingleton();
    if (!data) return;
    for (auto* perk : data->GetFormArray<RE::BGSPerk>()) {
        if (!perk || perk->IsDeleted() || perk->IsIgnored()) continue;
        const auto* rawName = perk->GetName();
        std::string name = rawName && rawName[0] ? rawName : "";
        if (name.empty()) {
            try {
                name = clib_util::editorID::get_editorID(perk);
            } catch (...) {}
        }
        if (name.empty()) name = "Unnamed perk";
        perks.push_back({ perk->GetFormID(), std::format("{} [{:08X}]", name, perk->GetFormID()) });
    }
    std::ranges::sort(perks, {}, &InternalFormInfo::displayName);
    logger::info("Loaded {} perks for Dynamic Two-Handed settings", perks.size());
}

void Manager::RefreshLists(std::string_view signatures) {
    if (signatures.empty() || Includes(signatures, "All") || Includes(signatures, "PERK")) {
        PopulateAllLists();
    }
}

const std::vector<InternalFormInfo>& Manager::GetPerks() const {
    return perks;
}
