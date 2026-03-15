// Game/Scenes/ShopSceneStates.h (新規作成)
#pragma once
#include "Engine/Platform/Input/Input.h"

class ShopScene; // 前方宣言

// ==========================================
// 状態の基底クラス（インターフェース）
// ==========================================
class IShopState {
  public:
    virtual ~IShopState() = default;
    virtual void Enter() {}
    virtual void Exit() {}
    virtual void Update(float dt) {}
};

// ==========================================
// プレイ中状態 (時間が進む)
// ==========================================
class ShopPlayingState : public IShopState {
  private:
    ShopScene *_scene;

  public:
    ShopPlayingState(ShopScene *scene) : _scene(scene) {}
    void Update(float dt) override; // 実装はShopScene.cppで
};

// ==========================================
// ポーズ中状態 (時間が止まる)
// ==========================================
class ShopPausedState : public IShopState {
  private:
    ShopScene *_scene;

  public:
    ShopPausedState(ShopScene *scene) : _scene(scene) {}
    void Enter() override;
    void Exit() override;
    void Update(float dt) override;
};