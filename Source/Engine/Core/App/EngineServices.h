#pragma once
#include <memory>

// 前方宣言
class FmodAudioManager;
class JoltPhysicsManager;
class InputFacade;

// ★ 追加: 描画関連の前方宣言
class Camera;
class LightManager;
class PostProcessManager;

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

    // グローバルな描画リソース
    std::shared_ptr<Camera>             camera;
    std::shared_ptr<LightManager>       lightManager;
    std::shared_ptr<PostProcessManager> postProcess;

private:
    EngineServices() = default;
};