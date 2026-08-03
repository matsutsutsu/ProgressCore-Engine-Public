#pragma once

#include <memory>
#include <string>

#include "Engine/Scene/Scene.h"

// ECS関連
#include "Engine/ECS/CCL_ECSContext.h"

#include "Engine/Scene/SceneManager.h"

#include "Engine/UI/ECS/UIRenderer.h" // ECSネイティブUIの2Dパス描画

class BaseScene : public Scene {
  protected:
    std::unique_ptr<CCL::ECS::ECSContext> m_ecsContext;
    CCL::ECS::World *m_worldPtr = nullptr;

    UIRenderer m_uiRenderer; // UIDrawList を2Dパスで描画する

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
    
    CCL::ECS::World* GetWorld() override { return m_worldPtr; }
    CCL::ECS::SystemManager* GetSystemManager() override { return m_ecsContext->GetSystemManager(); }

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



};