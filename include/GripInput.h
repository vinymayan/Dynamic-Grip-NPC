#pragma once

namespace GripInput {
    void RegisterListener();
    void HandleMessage(SKSE::MessagingInterface::Message* message);
    bool IsAvailable();
    void DrawMenu();
}
