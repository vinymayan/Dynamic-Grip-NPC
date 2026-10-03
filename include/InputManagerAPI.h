#pragma once

namespace InputManagerAPI {

    constexpr const char* PluginName = "InputManager";
    constexpr uint32_t kAPIVersion1 = 1;
    constexpr uint32_t kAPIVersion2 = 2;
    constexpr uint32_t kMessage_RequestAPI = 8085;
    constexpr uint32_t kMessage_ProvideAPI = 8086;
    constexpr uint32_t kMessage_RequestAPIV2 = 8087;
    constexpr uint32_t kMessage_ProvideAPIV2 = 8088;
    constexpr uint32_t VKEY_DIR_UP = 5000;
    constexpr uint32_t VKEY_DIR_DOWN = 5001;
    constexpr uint32_t VKEY_DIR_LEFT = 5002;
    constexpr uint32_t VKEY_DIR_RIGHT = 5003;
    constexpr uint32_t VKEY_DIR_UPRIGHT = 5004;
    constexpr uint32_t VKEY_DIR_UPLEFT = 5005;
    constexpr uint32_t VKEY_DIR_DOWNRIGHT = 5006;
    constexpr uint32_t VKEY_DIR_DOWNLEFT = 5007;

    struct ActionInfo {
        int id;
        const char* name;
        uint32_t pcMainKey;
        int pcMainAction;
        int pcMainTapCount;     
        uint32_t pcModifierKey;
        int pcModAction;
        int pcModTapCount;      
        uint32_t gamepadMainKey;
        int gamepadMainAction;
        int gamepadMainTapCount;
        uint32_t gamepadModifierKey;
        int gamepadModAction;
        int gamepadModTapCount; 
        int gamepadGestureStick;
        bool useCustomTimings; 
        float holdDuration;     
        float tapWindow;        
        bool isValid;
    };

    struct MotionInfo {
        int id;
        const char* name;
        float timeWindow;
        uint32_t pcSequence[20];
        int pcSequenceLength;
        uint32_t padSequence[20];
        int padSequenceLength;
        bool isValid;
    };

    enum class ActionUpdateCode : uint32_t {
        kSuccess = 0,
        kInvalidActionID,
        kInvalidMapping,
        kInvalidPCGesture,
        kInvalidGamepadGesture,
        kNameConflict,
        kPCMappingConflict,
        kGamepadMappingConflict,
        kPersistenceFailure,
        kInternalError
    };

    struct ActionUpdateResultV2 {
        uint32_t structSize = sizeof(ActionUpdateResultV2);
        uint32_t success = 0;  // 0 = failure, 1 = success
        ActionUpdateCode code = ActionUpdateCode::kInternalError;
        int32_t requestedActionID = -1;
        int32_t conflictingActionID = -1;
        char message[192]{};
    };
    static_assert(sizeof(ActionUpdateResultV2) == 212);

    class IInputManager {
    public:
        virtual ~IInputManager() = default;

        // ====================================================================
        // FUNÇÕES UNIFICADAS (inputType: 0 = Action, 1 = Motion, 2 = Gesture)
        // ====================================================================
        virtual size_t GetInputCount(int inputType) = 0;
        virtual const char* GetInputName(int inputType, int inputID) = 0;
        virtual int CreateInput(int inputType, const char* inputName) = 0;
        virtual bool DeleteInput(int inputType, int inputID) = 0;
        // Adicionando arrays de validação opcionais
        virtual void UpdateListener(int inputType, int inputID, const char* modName, const char* purpose, bool isRegistering, const int* validMainActions = nullptr, int mainCount = 0, const int* validModActions = nullptr, int modCount = 0) = 0;
        virtual size_t GetListenerCount(int inputType, int inputID) = 0;
        virtual const char* GetListenerModName(int inputType, int inputID, size_t index) = 0;

        // ====================================================================
        // FUNÇÕES DE MAPEAMENTO ESPECÍFICO (Mantidas Separadas)
        // ====================================================================
        virtual ActionInfo GetActionInfo(int actionID) = 0;
        virtual bool UpdateActionMapping(int actionID, const ActionInfo& newMapping) = 0;
        virtual MotionInfo GetMotionInfo(int motionID) = 0;
        virtual bool UpdateMotionMapping(int motionID, const MotionInfo& newMapping) = 0;
    };

    // V1 permanece congelada para compatibilidade binaria. Novas funcoes entram
    // somente em interfaces derivadas e sao solicitadas explicitamente.
    class IInputManagerV2 : public IInputManager {
    public:
        virtual uint32_t GetAPIVersion() const = 0;
        virtual ActionUpdateResultV2 UpdateActionMappingV2(
            int actionID,
            const ActionInfo& newMapping) = 0;
    };

    inline IInputManager* _API = nullptr;
    inline IInputManagerV2* _APIV2 = nullptr;

    // ====================================================================
    // MÉTODO 1: SKSE MESSAGING (Assíncrono)
    // ====================================================================
    inline void RequestAPI() {
        auto messaging = SKSE::GetMessagingInterface();
        if (messaging) {
            messaging->Dispatch(kMessage_RequestAPI, nullptr, 0, nullptr);
        }
    }

    inline void ReceiveAPI(SKSE::MessagingInterface::Message* message) {
        if (message->type == kMessage_ProvideAPI && message->data) {
            _API = static_cast<IInputManager*>(message->data);
        }
        else if (message->type == kMessage_ProvideAPIV2 && message->data) {
            _APIV2 = static_cast<IInputManagerV2*>(message->data);
            _API = _APIV2;
        }
    }

    inline void RequestAPIV2() {
        auto messaging = SKSE::GetMessagingInterface();
        if (messaging) {
            messaging->Dispatch(kMessage_RequestAPIV2, nullptr, 0, nullptr);
        }
    }

    // ====================================================================
    // MÉTODO 2: DLL EXPORT DIRECT (Síncrono / Instantâneo)
    // ====================================================================
    inline IInputManager* RequestAPIDirect() {
        // Tenta encontrar a DLL do Input Manager carregada na memória do Skyrim
        HMODULE handle = GetModuleHandleW(L"InputManager.dll");
        if (handle) {
            // Procura a função exportada pelo nome exato
            auto getApiFunc = (void* (*)())GetProcAddress(handle, "GetInputManagerAPI");
            if (getApiFunc) {
                _API = static_cast<IInputManager*>(getApiFunc());
                return _API;
            }
        }
        return nullptr;
    }

    inline IInputManagerV2* RequestAPIDirectV2() {
        HMODULE handle = GetModuleHandleW(L"InputManager.dll");
        if (handle) {
            auto getApiFunc =
                (void* (*)(uint32_t))GetProcAddress(
                    handle,
                    "GetInputManagerAPIEx");
            if (getApiFunc) {
                _APIV2 = static_cast<IInputManagerV2*>(
                    getApiFunc(kAPIVersion2));
                if (_APIV2) {
                    _API = _APIV2;
                }
                return _APIV2;
            }
        }
        return nullptr;
    }
}
