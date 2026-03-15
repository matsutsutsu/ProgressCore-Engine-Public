#pragma once
#include "Editor/Core/EditorWindowManager.h"
#include "Game/Scenes/BaseScene.h"


#include "Engine/GamePlay/Animation/AnimationSystem.h"

class EditorScene : public BaseScene {
    // エディタ固有
    std::unique_ptr<EditorWindowManager> _windowManager;

    // アニメーションシステムへの参照 (手動更新用)
    AnimationSystem* _animationSystem;

    // エディタ状態
    bool  m_isAnimEditMode = false; // アニメーション編集モード中か
    float m_animEditTime   = 0.0f;  // 現在のシーケンス時間


    CCL::ECS::EntityID selectedEntity = 0;

  public:
    EditorScene() = default;
    ~EditorScene() override {};

    void Initialize() override;
    void Finalize() override;
    // Finalize, Update, Render, FixedUpdate は BaseScene をそのまま使うため削除！

  protected:
    void RegisterSystems() override;

    // BaseSceneのUpdateをオーバーライドする
    void FixedUpdate(float dt) override;
    void Update(float dt) override;

    // フック関数で固有処理を注入
    void OnRender3D(RenderContext &rc) override;
    void OnDrawImGui() override;
    void OnPreLoadScene() override; // ロード時のパーティクル破棄用
};