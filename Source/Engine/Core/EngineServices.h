#pragma once
#include <memory>

// 前方宣言
class FmodAudioManager;
class JoltPhysicsManager;
class InputFacade;

// ゲームエンジン全体で共有されるコアシステムを保管する「仲介役」
class EngineServices {
public:
    static EngineServices& Instance() {
        static EngineServices instance;
        return instance;
    }

    std::shared_ptr<FmodAudioManager>   audioManager;
    std::shared_ptr<JoltPhysicsManager> physicsManager;
    std::shared_ptr<InputFacade>        inputFacade;

private:
    EngineServices() = default;
};