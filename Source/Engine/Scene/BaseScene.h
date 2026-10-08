#pragma once
// ===========================================================================
// 概要: ECS を使うシーン共通の基底クラス BaseScene。
//
// 責務:
//   - ECSContext（World + SystemManager）の生成と、共通リソースの World への登録
//   - 固定更新・毎フレーム更新・描画パイプライン・UI 描画の共通の流れ
//   - シーン終了時の後片付け（剛体・音・Entity の即時破棄）
//   - GPU デバイスロストからの復帰（既定では丸ごと作り直す）
//
// 備考: 派生クラスは RegisterSystems / OnRenderUI / OnDrawImGui を上書きして拡張する。
// ===========================================================================
#include <memory>
#include <string>

#include "Engine/Scene/Scene.h"

#include "Engine/ECS/CCL_ECSContext.h"

#include "Engine/Scene/SceneManager.h"

#include "Engine/UI/ECS/UIRenderer.h" // ECS の UI を2Dパスで描く

class BaseScene : public Scene
{
protected:
    std::unique_ptr<CCL::ECS::ECSContext> m_ecsContext;
    CCL::ECS::World                      *m_worldPtr = nullptr; // m_ecsContext の World（所有しない）

    UIRenderer m_uiRenderer; // UIDrawList を2Dパスで描く

public:
    BaseScene()                   = default;
    virtual ~BaseScene() override = default;

    virtual void Initialize() override;
    virtual void Start() override;
    virtual void Finalize() override;
    virtual void Update(float elapsedTime) override;
    virtual void FixedUpdate(float fixedTime) override;
    virtual void Render() override;  // 共通の描画パイプライン
    virtual void DrawGUI() override; // OnDrawImGui を呼ぶだけ

    CCL::ECS::World         *GetWorld() override { return m_worldPtr; }
    CCL::ECS::SystemManager *GetSystemManager() override { return m_ecsContext->GetSystemManager(); }

    /**
     * @brief GPU デバイスロストからの復帰後に Framework から呼ばれる。
     * @details デフォルト実装は ECS ワールドをリセットして Initialize() + Start() を
     *          再実行する（完全な再ロード）。
     *          ゲームの進行状態を保ちたい場合は派生クラスでオーバーライドすること。
     */
    virtual void OnDeviceRecreated();

protected:
    // ============================================================
    // 派生クラスでの拡張ポイント
    // ============================================================

    // このシーンで使う System を登録する（Initialize の最後に呼ばれる）
    virtual void RegisterSystems() {}

    // ECS の UI の後に、シーン独自の2D描画を行う
    virtual void OnRenderUI() {}

    // シーン独自の ImGui ウィンドウを描く
    virtual void OnDrawImGui() {}
};