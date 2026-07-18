#pragma once
#include <memory>

// 前方宣言
class FmodAudioManager;
class JoltPhysicsManager;
class InputFacade;

// 描画関連の前方宣言
class Camera;
class LightManager;
class PostProcessManager;

// ECSのジョブプール（前方宣言）
namespace CCL::ECS::Core { class JobSystem; }

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

    // ECSのフレーム並列で全Worldが共有する唯一のジョブプール。
    // スレッドプールはハードウェア資源（1コア1ワーカー）なので、World/SystemManager
    // ごとに持たず、上層で1つだけ所有して各 SystemManager へ注入する。
    // ※ シーン非同期ロード用のプールは別（SceneManager が持つ）。長時間ジョブを
    //    フレーム並列の WaitForCounter が work-stealing で拾う事故を防ぐため分離している。
    std::shared_ptr<CCL::ECS::Core::JobSystem> jobSystem;

    // グローバルな描画リソース
    std::shared_ptr<Camera>             camera;
    std::shared_ptr<LightManager>       lightManager;
    std::shared_ptr<PostProcessManager> postProcess;

private:
    EngineServices() = default;
};