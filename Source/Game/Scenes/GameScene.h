#pragma once
#include "Editor/Core/EditorWindowManager.h"
#include "Game/Scenes/BaseScene.h"

#include "Engine/Audio/Audio.h"
#include "Engine/Audio/AudioResource.h"

#include "Engine/UI/UIManager.h"
#include "Engine/UI/Text/TextManager.h"
#include "Game/UI/PlayerHUD.h"

#include "Engine/UI/Widgets/SimpleButton.h"

#include <SimpleMath.h>



class GameScene : public BaseScene {
    // Vector3の型エイリアス (これでGameScene内でVector3と書けるようになります)
    using Vector3 = DirectX::SimpleMath::Vector3;
    using Quaternion = DirectX::SimpleMath::Quaternion;

    // ゲームオーバー遷移用タイマー
    float _gameOverTimer = 0.0f;

      // ゲーム固有のメンバ
    std::unique_ptr<UIManager> _uiManager;
    std::unique_ptr<TextManager> _textManager;

    // HUDを一括管理
    std::unique_ptr<PlayerHUD> _playerHUD;


    // UI
    std::shared_ptr<UIElement>    _scorePanel;
    std::shared_ptr<SimpleButton> _nextBtn;



    
    std::unique_ptr<EditorWindowManager> _windowManager;
    CCL::ECS::EntityID                   selectedEntity = 0;

    // ポーズ管理用
    bool                                    _isPaused = false;
  

    // --- ボス撃破演出用の定義 ---
    enum class BossSequenceState {
        None,        // 通常プレイ中
        FocusBoss,   // ボスにカメラを向けてシェイク
        ShowScoreUI, // スコア表示・操作停止
        FocusGate,   // ゲート出現・カメラ移動
        ResumeGame   // プレイヤー操作再開
    };

    void UpdateBossSequence(float dt); // 毎フレーム呼ぶ演出管理関数

    // 修正: ターゲットを EntityID で指定するように変更
    void SpawnFocusCamera(CCL::ECS::EntityID targetID, float shakeAmount);
    void DestroyFocusCamera();                                          // 演出用カメラ削除

    // メンバ変数
    BossSequenceState _seqState = BossSequenceState::None;
    float             _seqTimer = 0.0f;

    CCL::ECS::EntityID _bossEntityID  = CCL::ECS::InvalidEntityID;
    CCL::ECS::EntityID _focusCameraID = CCL::ECS::InvalidEntityID;



    // ゲート出現位置
    Vector3 _gateSpawnPos = Vector3(0, 0, 0);

    AudioSource *bgm = nullptr;
  public:
    GameScene()                   = default;
    virtual ~GameScene() override = default;

    void Initialize() override;
    void Start() override;
    void Finalize() override;
    void Update(float elapsedTime) override;
    void FixedUpdate(float fixedElapsedTime) override;

    // ★追加: ポーズ切り替え関数
    void SetPause(bool pause);

  protected:
    virtual void RegisterSystems() override;



    // BaseSceneのフックを利用して描画
    void OnRenderUI() override;
    void OnRender3D(RenderContext &rc) override;
    void OnDrawImGui() override;
    void OnPreLoadScene() override; // ロード時のパーティクル破棄用
};