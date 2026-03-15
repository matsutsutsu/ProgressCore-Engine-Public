#include "GameScene.h"
#include "Game/Core/SystemGroups.h"
#include "Engine/Serialization/Factory/SceneInitializer.h"
#include "Game/Logics/World/LevelLoader.h"


#include "Engine/GamePlay/Core/Scene/SceneManager.h"
 
#include "Game/Scenes/SceneLoading.h" 

#include "Game/Logics/GameEvent/GameOverResource.h"

#include "Engine/Platform/Input/Input.h"

#include "Engine/Serialization/Factory/Prefab.h"
#include "Game/Logics/World/Generation/RoomTemplateManager.h"

#include "Engine/UI/Widgets/SimpleButton.h"
#include "Engine/UI/Widgets/UIProgressBar.h"

#include "Game/Logics/Combat/HealthComponent.h"

#include "ECS/System/CCL_SystemRegistry.h"

// Vector3などの型省略用
#include <SimpleMath.h>
using namespace DirectX::SimpleMath;

void GameScene::Initialize()
{
    // 1. UIManagerの初期化     BaseのInitializeでRegisterSystemあるのでここでする
    auto &graphics = Graphics::Instance();
    
    BaseScene::Initialize(); // 基盤初期化

    // エディタ固有の初期化
    _windowManager = std::make_unique<EditorWindowManager>();
    _windowManager->Initialize();

    // タイマー初期化
    _gameOverTimer = 0.0f;

 
 
    // ---------------------------------------------------------
    // ゲーム世界の初期化
    // ---------------------------------------------------------

    ID3D11Device *device = Graphics::Instance().GetDevice();

    // ★追加1: カメラをスポーンさせる (とりあえず動けるFreeCameraで確認)
    // 本番ではPlayerをスポーンさせ、それに追従するカメラに切り替える必要があります
    // CCL::Prefab::Spawn(_world, Prefab::ID::FreeCamera);

    // ゲームオーバーリソース実装
    _worldPtr->AddResource<GameOverResource>();

    // ★現在のステージレベルを取得
    int currentStage = SceneManager::Instance().GetPlayerData().stageLevel;

   

    // ★分岐: ステージ5はボス部屋、それ以外はランダム生成
    if (currentStage == 5) {
        // --- A. ボス部屋 (固定JSONロード) ---
        // 事前にMapDebugSystemで作っておいたボス部屋ファイルを指定

        LevelLoader::Load(_worldPtr, device, "Assets/GamePlay/StageTemplates/BossStage.json");

        // カメラを探す例
        auto player = _worldPtr->View<PlayerComponent>();
        if (!player.empty()) {

            _worldPtr->PatchComponent<TransformComponent>(player[0], [](TransformComponent &trans) {
                trans.position = {51.0f, 0.0f, 12.0f}; // プレイヤーを親にする
            });
        }

        // ボス演出用ライトをスポーン
        // ★修正: Prefab::ID::BossLighting -> "Assets/GamePlay/Prefabs/Light/BossLighting.json"
        Prefab::SpawnPrefab(*_worldPtr, "Assets/GamePlay/Prefabs/Light/BossLighting.json");

        // ボスをスポーン
        // ★修正: Prefab::ID::Boss -> "Assets/GamePlay/Prefabs/Enemy/Boss.json"
        // 座標指定がない場合はJSON内の座標が使われるか、(0,0,0)になるので注意。必要なら座標引数を追加。
        _bossEntityID = Prefab::SpawnPrefab(*_worldPtr, "Assets/GamePlay/Prefabs/Enemy/Boss.json" /*, {0,0,20} */);

        // BGMをボス戦用に変えるなどもここで可能

        bgm = Audio::Instance().LoadAudioSource("Assets/Audio/SE/wav/BossBGM.wav");
        // CCL::ECS::Core::EntityID bgmEntity = Prefab::Spawn(*_worldPtr, Prefab::ID::BGM_Boss);
    }
    else {

        SceneInitializer::SpawnGameLightSetting(*_worldPtr, device);

        // --- B. 通常ステージ (ランダム生成) ---
        // そのステージの設定を取得 (敵の強さや部屋サイズ確率など)
        MapGenerator::Config config = MapGenerator::GetStageConfig(currentStage);

        LevelLoader::LoadProcedural(_worldPtr, device, config);

   
    }
 
}

void GameScene::Start() 
{
    // 1. UIManagerの初期化     BaseのInitializeでRegisterSystemあるのでここでする
    auto &graphics = Graphics::Instance();
    _uiManager     = std::make_unique<UIManager>(graphics.GetDevice(), graphics.GetDeviceContext());

     // HUD初期化
    _playerHUD = std::make_unique<PlayerHUD>();
    _playerHUD->Initialize(_uiManager.get());

    // ゲームシーン開始時はすぐにHUDを表示
    _playerHUD->Show();

        // ★現在のステージレベルを取得
    int currentStage = SceneManager::Instance().GetPlayerData().stageLevel;

    // ステージ名を表示する
    std::string stageFilename;
    switch (currentStage) {
    case 1: stageFilename = "Assets/Textures/UI/Common/Stage1.png"; break;
    case 2: stageFilename = "Assets/Textures/UI/Common/Stage2.png"; break;
    case 3: stageFilename = "Assets/Textures/UI/Common/Stage3.png"; break;
    case 4: stageFilename = "Assets/Textures/UI/Common/Stage4.png"; break;
    case 5: stageFilename = "Assets/Textures/UI/Common/Stage5.png"; break;
    }
    _uiManager->LoadSprite("StageTitle", stageFilename.c_str());

    auto stageTitle = std::make_shared<UIElement>("StageTitle", stageFilename.c_str());
    stageTitle->SetPosition(Graphics::Instance().GetScreenWidth() / 2, 100);
    stageTitle->SetSize(400, 100);
    _uiManager->AddElement(stageTitle);
}

void GameScene::Finalize()
{
    if (bgm) {
        bgm->Stop();
        delete bgm;
    }


    _uiManager.reset();

    BaseScene::Finalize(); // 共通終了化
}

void GameScene::FixedUpdate(float elapsedTime)
{
    if (_isPaused) return;

    BaseScene::FixedUpdate(elapsedTime);
}

void GameScene::Update(float elapsedTime)
{
    // マウス情報の取得 (UI更新用)
    UIMouseState mouseState;
    // 2. 入力情報の取得
    auto &input    = Input::Instance();
    auto &mouse    = input.GetMouse();
    auto &keyboard = input.GetKeyboard();

    UIMouseState uiState;
    uiState.x = static_cast<float>(mouse.GetPositionX());
    uiState.y = static_cast<float>(mouse.GetPositionY());

    // 改良版ヘルパー関数を使用（ビット演算不要で可読性UP）
    uiState.isDown     = mouse.IsDown(Mouse::BTN_LEFT);      // 押されている
    uiState.isPressed  = mouse.IsTriggered(Mouse::BTN_LEFT); // 押された瞬間
    uiState.isReleased = mouse.IsReleased(Mouse::BTN_LEFT);  // 離された瞬間

    // 1. 入力検知 (ESCキーでポーズ切り替え)
    // ※Windows API直叩きの例。InputSystemがある場合はそちらを使ってください
    if (keyboard.IsTriggered('P')) {
        SetPause(!_isPaused);
    }


    //_textManager->Update(elapsedTime);

    // UIだけは動かす (ボタン操作のため)
    if (_uiManager) {
        _uiManager->Update(elapsedTime, mouseState);
    }

    if (_isPaused) return;

    // ★ HUD更新
    // プレイヤーのHPや所持金を反映
    auto &playerData = SceneManager::Instance().GetPlayerData();
    if (_playerHUD) {
        _playerHUD->Update(playerData,_worldPtr);
    }

    // これを忘れるとECS（全てのシステム）が動きません！
    BaseScene::Update(elapsedTime);

    // --- ボスシーケンス制御 ---
    UpdateBossSequence(elapsedTime);

    // 通常時はGameSceneの更新処理（ポーズ中なら止めるなど）
    if (_seqState == BossSequenceState::ShowScoreUI) {
        // UI表示中はプレイヤーの更新などを止める必要があればここで制御
        return;
    }

    // ※※※※※※※※※※※※※※
    // ポーズ画面との順番を考える！！！！！！！！！！！！！
    // ※※※※※※※※※※※※※※
    // 1. ゲームオーバーリソースをチェック
    if (_worldPtr->HasResource<GameOverResource>()) {
        auto &goRes = _worldPtr->GetResource<GameOverResource>();

        if (goRes.showScoreUI) {
            // ★ここでUIを表示する
            // _uiManager->ShowPanel("ScorePanel"); などを呼ぶ
            // または ImGui で描画フラグを立てる

            //_shouldShowScoreWindow = true; // メンバ変数などで管理
        }
    }
}

void GameScene::RegisterSystems()
{
    ID3D11Device *device = Graphics::Instance().GetDevice();

    auto &sysManager = *(_ecsContext->GetSystemManager());
    CCL::ECS::SystemRegistry::Instance().RegisterAll(sysManager);

  
}

void GameScene::OnRenderUI()
{
   
    if (_uiManager && _textManager) {
        _textManager->Render(true); // 3Dテキスト
        _uiManager->Render();       // HUD含むUI
    }

    // --------------------------------------------------------
    // ★修正: ゲームオーバー時の自動遷移処理
    // --------------------------------------------------------
    if (_worldPtr->HasResource<GameOverResource>()) {
        auto &goRes = _worldPtr->GetResource<GameOverResource>();

        if (goRes.showScoreUI) {
            // タイマーを加算
            _gameOverTimer += 0.01;

            // 一定時間経過後 (例: 3秒後) にショップ(タイトル)へ
            if (_gameOverTimer > 1.0f) {
                // プレイヤーデータ破棄 (以前のボタン処理と同じ)
                SceneManager::Instance().GetPlayerData().Reset();

                // シーン遷移
                SceneManager::Instance().ChangeScene("Shop");
            }
        }
    }
}

void GameScene::OnRender3D(RenderContext &rc){}


void GameScene::OnDrawImGui()
{
    EditorContext context;
    context.selectedEntity = &selectedEntity;
    context.world          = _worldPtr;
    context.systemManager  = _ecsContext->GetSystemManager(); // ★追加

    _windowManager->Draw(context);

    // ロード要求があればBaseSceneのパスに渡す
    if (!context.pendingLoadScenePath.empty()) {
        _pendingLoadScenePath = context.pendingLoadScenePath;
    }

    if (_windowManager->IsVisible()) {
        ImGui::Begin("System Info");
        _ecsContext->GetSystemManager()->OnGui();
        ImGui::End();

        // UIの位置調整用エディタを表示
        if (_uiManager) {
            _uiManager->DrawDebugGUI();
        }
    }
}

void GameScene::OnPreLoadScene()
{
    // システム破棄の前にパーティクルを破棄しないとクラッシュするため
    selectedEntity = 0;
}

// ★追加: ポーズ切り替え処理
void GameScene::SetPause(bool pause)
{
    _isPaused = pause;

 
    // カーソル表示切り替え (ポーズ中はカーソルを出すなど)
    // ShowCursor(pause);
}

void GameScene::UpdateBossSequence(float dt)
{
    // 1. ボスのIDを取得（まだ持っていなければ）
    if (!_worldPtr->IsEntityValid(_bossEntityID)) {
        auto view = _worldPtr->View<BossComponent>();
        if (!view.empty()) {
            _bossEntityID = view[0];
        }
    }

    switch (_seqState) {
    case BossSequenceState::None: {
        // ボスの死亡監視
        if (_worldPtr->IsEntityValid(_bossEntityID)) {
            auto *boss   = _worldPtr->GetComponent<BossComponent>(_bossEntityID);
            auto *health = _worldPtr->GetComponent<HealthComponent>(_bossEntityID);

            // HP0になったら演出開始
            if (health && health->currentHealth <= 0) {
                _seqState = BossSequenceState::FocusBoss;
                _seqTimer = 0.0f;

                // ゲート出現位置を保存（ボスの位置）
                auto *trans   = _worldPtr->GetComponent<TransformComponent>(_bossEntityID);
                _gateSpawnPos = trans->GetWorldPosition();

                // カメラ生成（ターゲットはボス）
                SpawnFocusCamera(_bossEntityID, 1.5f);
            }
        }
        break;
    }

    case BossSequenceState::FocusBoss: {
        // カメラシェイクしてボスを映す期間 (例えば3秒)
        _seqTimer += dt;
        if (_seqTimer > 3.0f) {
            DestroyFocusCamera(); // カメラをプレイヤーに戻す

            // ★ここでボスを消すと安全
            if (_worldPtr->IsEntityValid(_bossEntityID)) {
                _worldPtr->Destroy(_bossEntityID);
            }

            // 次のフェーズへ
            _seqState = BossSequenceState::ShowScoreUI;
            _seqTimer = 0.0f; // ★重要: タイマーリセット！

            // UI表示
            //_scorePanel->SetActive(true);
            //_scorePanel->MoveTo(Graphics::Instance().GetScreenWidth() / 2, 300); // 画面中央へ

            //_nextBtn->SetActive(true); // ボタン有効化

            // マウスカーソルを表示・操作可能にする処理が必要ならここで行う
            // Input::Instance().SetCursorVisible(true);
        }
        break;
    }

    case BossSequenceState::ShowScoreUI: {
        // プレイヤー操作禁止 (InputSystem側でGameStateを見て止めるか、ここで入力を上書き)
        // ボタンクリック待ち (Initializeのラムダ式で遷移)

        _seqTimer += dt;
        if (_seqTimer > 3.0f) {
            _seqState = BossSequenceState::FocusGate;
            _seqTimer = 0.0f; // ★重要: タイマーリセット！
        }

        break;
    }

    case BossSequenceState::FocusGate: {
        // ゲート生成 (Prefab::ID::Gate があると仮定)
        // まだ生成してなければ生成
        if (_seqTimer == 0.0f) {
            // ゲートをスポーンし、そのIDを取得する
            // ※Prefab::SpawnがEntityIDを返すと仮定
            CCL::ECS::EntityID gateID =
                Prefab::SpawnPrefab(*_worldPtr, "Assets/GamePlay/Prefabs/Map/Portal", _gateSpawnPos);

            // カメラ生成（ターゲットはゲート）
            SpawnFocusCamera(gateID, 0.0f);
        }

        _seqTimer += dt;
        // 2秒間ゲートを見せる
        if (_seqTimer > 2.0f) {
            DestroyFocusCamera(); // プレイヤー視点に戻る
            _seqState = BossSequenceState::ResumeGame;
        }
        break;
    }

    case BossSequenceState::ResumeGame: {
        // プレイヤー操作再開
        // プレイヤーがゲートに入ると、既存のGateSystemが作動してタイトルへ戻る
        break;
    }
    }
}

// 演出用カメラの生成 (修正版)
// 引数を EntityID に変更し、誰でも追従できるようにした
void GameScene::SpawnFocusCamera(CCL::ECS::EntityID targetID, float shakeAmount)
{
    DestroyFocusCamera();

    // ターゲットが無効なら何もしない
    if (!_worldPtr->IsEntityValid(targetID)) return;

    // アーキタイプ生成
    auto arch = CCL::ECS::ArchetypeHelper::
        Generate<TransformComponent, VirtualCamera, CameraBodyFollow, CameraAimLookAt>();

    _focusCameraID = _worldPtr->RequestSpawnEntity(arch);

    // -----------------------------------------------------------
    // 1. ターゲットの向きに合わせてオフセットを計算
    // -----------------------------------------------------------
    Vector3 calculatedOffset = {0.0f, 5.0f, -8.0f}; // デフォルト

    auto *targetTrans = _worldPtr->GetComponent<TransformComponent>(targetID);
    if (targetTrans) {
        // ターゲットの前方ベクトルを取得
        Vector3 forward = Vector3::Transform(Vector3::UnitZ, targetTrans->rotation);

        // 水平化
        forward.y = 0.0f;
        forward.Normalize();

        // ボスなら正面、ゲートなら正面（あるいは固定）
        // ここでは共通ロジックとして「前方から離れた位置」に置く
        float distance     = 18.0f;
        calculatedOffset   = (forward * distance);
        calculatedOffset.y = 10.5f;
    }

    // -----------------------------------------------------------
    // 2. 追従設定 (Body) - 渡された targetID をセット
    // -----------------------------------------------------------
    _worldPtr->PatchComponent<CameraBodyFollow>(_focusCameraID, [&](CameraBodyFollow &body) {
        body.target  = targetID; // ★ここが重要（ボス固定にしない）
        body.offset  = calculatedOffset;
        body.damping = 5.0f;
    });

    // -----------------------------------------------------------
    // 3. 注視設定 (Aim) - 渡された targetID をセット
    // -----------------------------------------------------------
    _worldPtr->PatchComponent<CameraAimLookAt>(_focusCameraID, [&](CameraAimLookAt &aim) {
        aim.target  = targetID; // ★ここも変更
        aim.offset  = {0.0f, 2.0f, 0.0f};
        aim.damping = 5.0f;
    });

    // 4. VirtualCamera基本設定
    _worldPtr->PatchComponent<VirtualCamera>(_focusCameraID, [&](VirtualCamera &vcam) {
        vcam.priority  = 100;
        vcam.fov       = 45.0f;
        vcam.blendTime = 1.5f;
        vcam.isValid   = false;
    });

    // シェイク設定
    if (shakeAmount > 0.0f) {
        _worldPtr->AddComponent<CameraShake>(_focusCameraID, CameraShake{});
        _worldPtr->PatchComponent<CameraShake>(
            _focusCameraID, [&](CameraShake &s) { s.SetShake(3.0f, shakeAmount); });
    }

    _worldPtr->ScrutinyAndApply();
}

void GameScene::DestroyFocusCamera()
{
    if (_worldPtr->IsEntityValid(_focusCameraID)) {
        _worldPtr->Destroy(_focusCameraID);
        _focusCameraID = CCL::ECS::InvalidEntityID;
        _worldPtr->ScrutinyAndApply();
    }
}