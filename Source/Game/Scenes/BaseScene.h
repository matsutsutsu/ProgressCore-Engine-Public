#pragma once

#include <memory>
#include <string>

#include "Engine/Scene/Scene.h"

// ECS関連
#include "Engine/ECS/Common/CCL_Common.h"
#include "Engine/ECS/Core/CCL_PendingOps.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemManager.h"
#include "Engine/ECS/CCL_ECSContext.h"

#include "Engine/Scene/SceneManager.h"

class BaseScene : public Scene {
  protected:
    std::unique_ptr<CCL::ECS::ECSContext> _ecsContext;
    CCL::ECS::Core::World *_worldPtr = nullptr;

    // セーブ・ロードの「予約」パス
    std::string _pendingLoadScenePath = "";
    std::string _pendingSaveScenePath = ""; 

  public:
    BaseScene()                   = default;
    virtual ~BaseScene() override = default;

    // 基本的なライフサイクル
    virtual void Initialize() override;
    virtual void Start() override; // GPUセットアップなど、Initialize後にメインスレッドで1回だけ呼ばれる処理
    virtual void Finalize() override;
    virtual void Update(float elapsedTime) override;
    virtual void FixedUpdate(float fixedTime) override;
    virtual void Render() override;  // 共通レンダリングパイプライン
    virtual void DrawGUI() override; // ImGuiエントリポイント

    // =========================================================
    // 外部（ImGui等）から呼ばれる「安全なリクエスト関数」
    // =========================================================
    void RequestLoadScene(const std::string& path) { _pendingLoadScenePath = path; }
    void RequestSaveScene(const std::string& path) { _pendingSaveScenePath = path; }

    CCL::ECS::Core::World* GetWorld() override { return _worldPtr; }
    CCL::ECS::SystemManager* GetSystemManager() override { return _ecsContext->GetSystemManager(); }

    /**
     * @brief GPU デバイスロストからの復帰後に Framework から呼ばれる。
     * @details デフォルト実装は ECS ワールドをリセットして Initialize() + Start() を
     *          再実行する（完全な再ロード）。
     *          ゲームの進行状態を保ちたい場合は派生クラスでオーバーライドすること。
     */
    virtual void OnDeviceRecreated();

  protected:
    // --- 派生クラスでの拡張ポイント ---

    // システム登録 (必須)
    virtual void RegisterSystems() {}

    // ゲーム内UI描画 (Sprite, HealthBarなど)
    virtual void OnRenderUI() {}

    // ImGui描画 (Window, Inspectorなど)
    virtual void OnDrawImGui() {}

    // リクエストを消化する関数
    void ExecutePendingSceneOperations();

};