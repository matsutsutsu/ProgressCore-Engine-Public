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

    std::shared_ptr<IAudioAPI>           audioManager;   // オーディオ（実体は起動時に選ぶ。現在は DxtkAudioManager）
    std::shared_ptr<JoltPhysicsManager>  physicsManager; // 物理（Jolt）
    std::shared_ptr<InputFacade>         inputFacade;    // ゲーム用の入力（InputConfig.json の割り当てでアクション・軸として読む）
    std::shared_ptr<CCL::ECS::JobSystem> jobSystem;      // ECS のフレーム並列で全 World が共有する唯一のジョブプール（シーンのロード用は別）

    // --- 全シーンで共有する描画リソース ---

    std::shared_ptr<Camera>             camera;       // 描画に使うカメラ
    std::shared_ptr<LightManager>       lightManager; // ライト
    std::shared_ptr<PostProcessManager> postProcess;  // ポストプロセス

private:
    EngineServices() = default;
};