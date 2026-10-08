#pragma once
// ===========================================================================
// 概要: エンジン全体で共有するコアサービスを1か所に預かるシングルトン。
//
// 責務:
//   - オーディオ・物理・入力・ジョブプール・カメラ・ライト・ポストプロセスの保持
//
// 備考: 生成と初期化は Framework のコンストラクタで行う。ここは預かるだけ。
// ===========================================================================
#include <memory>

class IAudioAPI; // オーディオバックエンド抽象（実体は DxtkAudioManager 等）
class JoltPhysicsManager;
class InputFacade;

class Camera;
class LightManager;
class PostProcessManager;

namespace CCL::ECS
{
    class JobSystem;
}

class EngineServices
{
public:
    static EngineServices &Instance()
    {
        static EngineServices instance;
        return instance;
    }

    std::shared_ptr<IAudioAPI>          audioManager; // 実体は起動時に選ぶ（現在は DxtkAudioManager）
    std::shared_ptr<JoltPhysicsManager> physicsManager;
    std::shared_ptr<InputFacade>        inputFacade;

    // ECS のフレーム並列で全 World が共有する唯一のジョブプール。
    // スレッドプールはハードウェア資源（1コア1ワーカー）なので、World/SystemManager
    // ごとに持たず、上層で1つだけ所有して各 SystemManager へ注入する。
    // ※ シーン非同期ロード用のプールは別（SceneManager が持つ）。長時間ジョブを
    //    フレーム並列の WaitForCounter が work-stealing で拾う事故を防ぐため分離している。
    std::shared_ptr<CCL::ECS::JobSystem> jobSystem;

    // 全シーンで共有する描画リソース
    std::shared_ptr<Camera>             camera;
    std::shared_ptr<LightManager>       lightManager;
    std::shared_ptr<PostProcessManager> postProcess;

private:
    EngineServices() = default;
};