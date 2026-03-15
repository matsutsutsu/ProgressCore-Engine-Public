#pragma once
#include "Editor/Core/EditorWindowManager.h"
#include "Game/Scenes/BaseScene.h"


#include "Engine/Audio/Audio.h"
#include "Engine/Audio/AudioResource.h"

#include "Engine/UI/Text/TextManager.h"

#include "Game/UI//PauseMenuUI.h"
#include "Game/Scenes/SceneStates/ShopSceneStates.h"

#include "Game/UI/InventoryEvent.h"
#include "Game/UI/StoreEvent.h"

#include "Game/UI/PlayerHUD.h"

#include "Engine/UI/UIManager.h"
#include "Engine/UI/Widgets/SimpleButton.h"
//#include "Engine/UI/Widgets/UIProgressBar.h"
//#include "Engine/UI/Widgets/UIText.h"

class ShopScene : public BaseScene {
  public:
    ShopScene()                   = default;
    virtual ~ShopScene() override = default;

    void         Initialize() override;
    void         Start() override;
    void         Finalize() override;
    void         Update(float elapsedTime) override;
    virtual void FixedUpdate(float fixedTime) override;

  protected:
    virtual void RegisterSystems() override;

    // 描画関連フック
    void OnRenderUI() override;
    void OnRender3D(RenderContext &rc) override;
    void OnDrawImGui() override;
    void OnPreLoadScene() override;

  private:
    // --- 初期化ヘルパー関数 ---
    void InitializeUI(bool isReturning);        // UI生成関連
    void InitializeWorld(bool isReturning);     // ステージ・ライト・ECSリソース
    void InitializeCamera(bool isReturning);// タイトル用カメラの生成

    // --- 更新ヘルパー関数 ---
    void UpdateTitle(float dt);    // タイトル画面中の処理
    void UpdateGameplay(float dt); // ゲームプレイ中の処理

    // --- アクション ---
    void StartGame(); // ゲーム開始遷移

public:
    // 状態を切り替える仕組み
    void ChangeState(std::unique_ptr<IShopState> newState) {
        if (_currentState) _currentState->Exit();
        _currentState = std::move(newState);
        if (_currentState) _currentState->Enter();
    }

    // Stateから呼ばれる公開関数
    void ChangeToPlayingState();
    void ChangeToPausedState();
    void UpdateECS(float dt) { if (_ecsContext) _ecsContext->UpdateLogic(dt); }
    void ShowPauseMenu() { if (_pauseMenu) _pauseMenu->Show(); }
    void HidePauseMenu() { if (_pauseMenu) _pauseMenu->Hide(); }

  private:
    // システム・マネージャー
  
    std::unique_ptr<EditorWindowManager>     _windowManager;


    std::unique_ptr<TextManager> _textManager;
    std::unique_ptr<UIManager>   _uiManager;

    // ステートマシンとUI
    std::unique_ptr<IShopState>  _currentState;
    std::unique_ptr<PauseMenuUI> _pauseMenu;



    //std::unique_ptr<InventorySystem> inventorySystem;
    //std::unique_ptr<StoreSystem>     storeSystem;

    // UI要素
    std::shared_ptr<UIElement>    _titleImg;
    std::shared_ptr<SimpleButton> _startBtn;
    std::shared_ptr<SimpleButton> _exitBtn;

   // HUDを一括管理するクラスに変更
    std::unique_ptr<PlayerHUD> _playerHUD;

    // ECS Entity管理
    CCL::ECS::EntityID _titleCameraID = CCL::ECS::InvalidEntityID;
    CCL::ECS::EntityID selectedEntity = 0;

    // フラグ
    bool _isPaused = false;


    // HUD表示制御用タイマー
    float       _gameplayTimer    = 0.0f;
    bool        _hasHudStartAnim  = false;
    float CAMERA_BLEND_TIME = 2.0f; // カメラ遷移時間と合わせる

    AudioSource *bgm = nullptr;
};