#pragma once

#include "ClibUtil/editorID.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace FormUtil {
    std::string NormalizeFormID(RE::TESForm* form);
    RE::FormID FormIDFromString(const std::string& value);
}

struct InternalFormInfo {
    RE::FormID formID = 0;
    std::string displayName;
};

class Manager {
public:
    static Manager* GetSingleton();
    void PopulateAllLists();
    void RefreshLists(std::string_view signatures);
    const std::vector<InternalFormInfo>& GetPerks() const;

private:
    std::vector<InternalFormInfo> perks;
};
