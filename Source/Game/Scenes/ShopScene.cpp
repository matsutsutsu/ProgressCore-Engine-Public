#include "ShopScene.h"

// --- Core / ECS ---
#include "ECS/System/CCL_System.h"
#include "Game/Core/SystemGroups.h"
#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Serialization/Factory/SceneInitializer.h"

// --- Logic / Components ---
#include "Game/Logics/World/LevelLoader.h"
#include "Game/Logics/Combat/HealthComponent.h"
#include "Game/Logics/Combat/Weapon/Core/GunComponent.h"


// --- Input / Utils ---
#include "Engine/Platform/Input/Input.h"
#include "Engine/GamePlay/Core/Scene/SceneManager.h"
#include <algorithm>

#include "Engine/Scripting/LuaScriptComponent.h"

#include "ECS/System/CCL_SystemRegistry.h"


using namespace DirectX::SimpleMath;


void ShopScene::Initialize()
{
    // マネージャー類の初期化
    auto &graphics = Graphics::Instance();
    _windowManager = std::make_unique<EditorWindowManager>();
    _windowManager->Initialize();

    // 1. TextManagerの初期化 (フォントロード)
    _textManager = std::make_unique<TextManager>();

    // 基盤初期化
    BaseScene::Initialize();

    // ★判定: プレイヤーデータが存在するか？
    // hasData == true なら「ゲームから戻ってきた」、falseなら「初回/リセット」
    bool isReturning = SceneManager::Instance().GetPlayerData().hasData;

    // 各セクションの初期化実行
    
    InitializeWorld(isReturning);
    InitializeCamera(isReturning);


}

void ShopScene::Start() 
{
    // まずベースのStartを呼ぶ
    BaseScene::Start();

    auto &graphics = Graphics::Instance();
    auto *device   = graphics.GetDevice();
    auto *dc       = graphics.GetDeviceContext();

    // ========================================================
    // 現場監督(dc)を必要とするオブジェクトは、必ずここで生成・構築する！
    // ========================================================

    // 1. UIManager の生成 (内部で dc を保持して使うため)
    _uiManager = std::make_unique<UIManager>(device, dc);

    // UIManagerにTextManagerを渡す (これでUITextが描画できるようになる)
    _uiManager->SetTextManager(_textManager.get());

    // 判定: プレイヤーデータが存在するか？
    // hasData == true なら「ゲームから戻ってきた」、falseなら「初回/リセット」
    bool isReturning = SceneManager::Instance().GetPlayerData().hasData;

    // 3. UIパーツの組み立て
    // ※ UIManagerが生成されていないと作れないため、ここで呼ぶ
    InitializeUI(isReturning);


    // ポーズメニューの生成とコールバック登録
    // _pauseMenu = std::make_unique<PauseMenuUI>();
    // _pauseMenu->Initialize(
    //     _uiManager.get(),
    //     [this]() { ChangeToPlayingState(); }, // 再開ボタンが押されたらプレイ状態へ
    //     [this]() { SceneManager::Instance().ChangeScene("TitleScene"); } // タイトルへ戻る
    // );

    // 初期状態を「プレイ中」にセットする
    ChangeToPlayingState();
}

void ShopScene::InitializeUI(bool isReturning)
{
    if (!_uiManager) return;

    // --- 画像ロード ---
    _uiManager->LoadSprite("TitleImg", "Assets/Textures/UI/Title/Title_Logo.png");
    _uiManager->LoadSprite("StartBtnImg", "Assets/Textures/UI/Title/Title_GameStart.png");
    _uiManager->LoadSprite("ExitBtnImg", "Assets/Textures/UI/Title/Title_GameEnd.png");
  
    //  HUDの生成と初期化
    _playerHUD = std::make_unique<PlayerHUD>();
    _playerHUD->Initialize(_uiManager.get());

    float screenH = (float)Graphics::Instance().GetScreenHeight();

    // =========================================================
    // タイトル画面
    // =========================================================
    if (!isReturning) {
        // タイトルロゴ
        _titleImg = std::make_shared<UIElement>("TitleImg", "TitleImg");
        // ※SetNativeSizeはUIManagerではなく、Element側で管理するか、Manager経由で呼ぶ
        // 今回は手動サイズ指定、または既存の仕組みを利用
        _titleImg->SetSize(800, 400); // 仮サイズ
        _titleImg->SetPosition(1920 / 3.0f, -500);
        _titleImg->MoveTo(1920 / 3.0f, 300);
        _uiManager->AddElement(_titleImg);

        // スタートボタン
        _startBtn = std::make_shared<SimpleButton>(
            "StartBtn", "StartBtnImg", [this]() { this->StartGame(); });
        _startBtn->SetSize(400, 100);
        _startBtn->SetScale(0.5f);
        _startBtn->SetPosition(-2000, 540);
        _startBtn->MoveTo(460, 540);
        _startBtn->SetDelay(0.5f);
        _uiManager->AddElement(_startBtn);

        // 終了ボタン
        _exitBtn =
            std::make_shared<SimpleButton>("ExitBtn", "ExitBtnImg", []() { PostQuitMessage(0); });
        _exitBtn->SetSize(400, 100);
        _exitBtn->SetScale(0.5f);
        _exitBtn->SetPosition(-2000, 740);
        _exitBtn->MoveTo(460, 740);
        _exitBtn->SetDelay(0.7f);
        _uiManager->AddElement(_exitBtn);
    }
    else {
        // ゲームに戻ってきた場合は最初からHUDを表示
        if (_playerHUD) _playerHUD->Show();
        _hasHudStartAnim = true;
    }

  

    // BGM
    //bgm = Audio::Instance().LoadAudioSource("Assets/Audio/SE/wav/ShopBGM.wav");
    //if (bgm) {
    //    bgm->Play(true);
    //    bgm->SetVolume(0.1f);
    //}
}

void ShopScene::InitializeWorld(bool isReturning)
{
    ID3D11Device *device = Graphics::Instance().GetDevice();

    // ロジックシステム
    //pauseSystem     = std::make_unique<PauseSystem>(_uiManager.get(), _textManager.get());
    //inventorySystem = std::make_unique<InventorySystem>(_uiManager.get(), _textManager.get());
    //storeSystem = std::make_unique<StoreSystem>(_uiManager.get(), _textManager.get(), inventorySystem.get());

    // ライト・レベル
    SceneInitializer::SpawnGameLightSetting(*_worldPtr, device);
    LevelLoader::Load(_worldPtr, device, "Assets/GamePlay/StageTemplates/ShopStage.json");

    _worldPtr->ScrutinyAndApply();

    // ---------------------------------------------------
    // 1. プレイヤーIDを探す
    // ---------------------------------------------------
    CCL::ECS::EntityID playerID   = 0;
    auto               playerView = _worldPtr->View<PlayerComponent>();
    if (!playerView.empty()) {
        playerID = playerView[0];

        // (ついでにプレイヤーの向き調整があればここで)
        _worldPtr->PatchComponent<TransformComponent>(playerID, [](TransformComponent &t) {
            float             yawRadians = DirectX::XMConvertToRadians(80.0f);
            DirectX::XMVECTOR rotQ =
                DirectX::XMQuaternionRotationRollPitchYaw(0.0f, yawRadians, 0.0f);
            DirectX::XMStoreFloat4(&t.rotation, rotQ);
        });
    }

    // ゲート召喚 (Prefab::Spawn -> SpawnPrefab)
    Prefab::SpawnPrefab(*_worldPtr, "Assets/GamePlay/Prefabs/World/Map/Portal", {38.0f, 1.0f, 18.0f});

    // ---------------------------------------------------
    // 3. ゲーム用カメラ (FollowCamera) を生成し、リンクする
    // ---------------------------------------------------
    // ★JSON内の priority は "10" (低め) になっています
    //CCL::ECS::EntityID gameCameraID =
    //    Prefab::SpawnPrefab(*_worldPtr, "Assets/GamePlay/Prefabs/Camera/FollowCamera.json");

    //// ★最重要: プレイヤーが見つかったら、カメラのターゲットを上書きする
    //if (playerID != 0 && gameCameraID != CCL::ECS::InvalidEntityID) {

    //    // "追従" ターゲットの設定
    //    _worldPtr->PatchComponent<CameraBodyFollow>(
    //        gameCameraID, [playerID](CameraBodyFollow &body) { body.target = playerID; });

    //    // "注視" ターゲットの設定
    //    _worldPtr->PatchComponent<CameraAimLookAt>(
    //        gameCameraID, [playerID](CameraAimLookAt &aim) { aim.target = playerID; });
    //}

    // ★重要: ゲームステートの初期値を切り替え
    if (isReturning) {
        _worldPtr->AddResource<GameState>(GameState{GameStateMode::Gameplay});
    }
    else {
        _worldPtr->AddResource<GameState>(GameState{GameStateMode::Title});
    }
}

void ShopScene::InitializeCamera(bool isReturning)
{
    // ★戻ってきた場合はタイトルカメラを作らず、プレイヤーカメラ(デフォルト)を使用
    if (isReturning) return;

    // プレイヤー位置を基準にカメラを配置
    auto players = _worldPtr->View<PlayerComponent, TransformComponent>();
    if (players.empty()) return;

    EntityID playerID    = players[0];
    auto    *playerTrans = _worldPtr->GetComponent<TransformComponent>(playerID);
    Vector3  playerPos   = playerTrans->GetWorldPosition();

    // ShopScene.cpp の Initialize 内などで
    _worldPtr->PatchComponent<TransformComponent>(playerID, [&](TransformComponent &t) {
        // 好きな向きに設定 (例: 180度回転してカメラの方を向く)
        t.rotation = Quaternion::CreateFromAxisAngle(Vector3::Up, DirectX::XM_PI);
    });

    // タイトル用カメラの生成 (Prefab::Spawn -> SpawnPrefab)
    _titleCameraID = Prefab::SpawnPrefab(*_worldPtr, "Assets/GamePlay/Prefabs/Camera/TitleCamera");

    _worldPtr->ScrutinyAndApply();

    auto *cameraEntity = _worldPtr->GetComponent<VirtualCamera>(_titleCameraID);
    // ブレンドタイムを操作可能、UI表示タイミングのタイマーにセット
    CAMERA_BLEND_TIME = cameraEntity->blendTime - 0.6f;
}

// ゲーム開始処理（ボタンとキー入力の共通処理）
void ShopScene::StartGame()
{
    if (!_worldPtr->HasResource<GameState>()) return;
    auto &state = _worldPtr->GetResource<GameState>();

    // 既にゲーム中なら何もしない
    if (state.mode == GameStateMode::Gameplay) return;

    // 1. 状態遷移
    state.mode = GameStateMode::Gameplay;

    // タイマーリセット
    _gameplayTimer   = 0.0f;
    _hasHudStartAnim = false;

    // タイトルカメラの優先度を下げて追従カメラに遷移する
    if (_worldPtr->IsEntityValid(_titleCameraID)) {
        auto *vcam = _worldPtr->GetComponent<VirtualCamera>(_titleCameraID);
        if (vcam) {
            vcam->priority = 0;
        }
    }

    // 3. タイトルUIアニメーション（退場）
    if (_titleImg) _titleImg->MoveTo(1920 / 3.0f, -500);
    if (_startBtn) _startBtn->MoveTo(-500, 540);
    if (_exitBtn) _exitBtn->MoveTo(-500, 740);

    // ★ HUDを入場させる
    if (_playerHUD) _playerHUD->Show();
    _hasHudStartAnim = true;

    // UIProgressBarなどをここで有効化・画面内移動させると綺麗です
}

void ShopScene::Finalize()
{
    if (bgm) {
        bgm->Stop();
        delete bgm;
    }

    // シーン破棄時にUIも安全に解放する
    _uiManager.reset();

    BaseScene::Finalize(); // 共通終了化
}

void ShopScene::Update(float elapsedTime)
{
    BaseScene::Update(elapsedTime);

    if (!_worldPtr->HasResource<GameState>()) return;
    auto mode = _worldPtr->GetResource<GameState>().mode;

    // 状態に応じた処理の分岐
    if (mode == GameStateMode::Title) {
        UpdateTitle(elapsedTime);
    }
    else if (mode == GameStateMode::Gameplay) {
        UpdateGameplay(elapsedTime);
    }

    // --- UI更新 ---
    if (_uiManager) {
        auto &input = Input::Instance();
        auto &mouse = input.GetMouse();

        UIMouseState uiState;
        uiState.x          = static_cast<float>(mouse.GetPositionX());
        uiState.y          = static_cast<float>(mouse.GetPositionY());
        uiState.isDown     = mouse.IsDown(Mouse::BTN_LEFT);
        uiState.isPressed  = mouse.IsTriggered(Mouse::BTN_LEFT);
        uiState.isReleased = mouse.IsReleased(Mouse::BTN_LEFT);

        _uiManager->Update(elapsedTime, uiState);
    }
}

void ShopScene::UpdateTitle(float dt)
{
    // タイトル画面でのキー入力（SPACE / GamePad A）
    bool isStartPressed = (Input::Instance().GetGamePad().GetButtonDown() & GamePad::BTN_A) ||
                          Input::Instance().GetKeyboard().IsTriggered(VK_SPACE);

    if (isStartPressed) {
        StartGame();
    }
}

void ShopScene::UpdateGameplay(float dt)
{
    // ★ プレイヤーデータの更新をHUDに反映
    auto &playerData = SceneManager::Instance().GetPlayerData();
    if (_playerHUD) {
        _playerHUD->Update(playerData,_worldPtr);
    }

    // インベントリ・ストアの操作
    for (auto entity : _worldPtr->View<PlayerComponent>()) {
        auto gunComp = _worldPtr->GetComponent<GunComponent>(entity);
        if (!gunComp) continue;

        //InventoryEvent inventoryEvent{&gunComp->gun};
        //StoreEvent     storeEvent{};

        static bool isInventoryOpen = false;
        static bool isStoreOpen     = false;

        //// インベントリ開閉 (Eキー/ボタン)
        //if (!isStoreOpen && (Input::Instance().GetGamePad().GetButtonDown() & GamePad::BTN_E)) {
        //    isInventoryOpen = !isInventoryOpen;
        //    if (isInventoryOpen) {
        //        inventorySystem->OpenInventory(inventoryEvent);
        //        storeSystem->Close(storeEvent);
        //    }
        //    else {
        //        inventorySystem->CloseInventory(inventoryEvent);
        //    }
        //}

        //// ストア開閉 (Qキー/ボタン)
        //if (!isInventoryOpen && (Input::Instance().GetGamePad().GetButtonDown() & GamePad::BTN_Q)) {
        //    isStoreOpen = !isStoreOpen;
        //    if (isStoreOpen) {
        //        storeSystem->Open(storeEvent);
        //        inventorySystem->CloseInventory(inventoryEvent);
        //    }
        //    else {
        //        storeSystem->Close(storeEvent);
        //    }
        //}

        //if (isInventoryOpen) inventorySystem->UpdateInventory(inventoryEvent);
        //if (isStoreOpen) storeSystem->Update(storeEvent);
    }
}



void ShopScene::FixedUpdate(float fixedTime)
{
    if (_isPaused) return;

    BaseScene::FixedUpdate(fixedTime);
}

void ShopScene::RegisterSystems()
{
    // SystemGroups.h のインクルードや個別の登録呼び出しはすべて削除！
    // これだけで、マクロを書いた全システムが自動的に SystemManager に登録されます。
    auto &sysManager = *(_ecsContext->GetSystemManager());
    CCL::ECS::SystemRegistry::Instance().RegisterAll(sysManager);
  
}

void ShopScene::OnRenderUI()
{
   

    if (_uiManager && _textManager) {
        // 1. 3Dフローティングテキスト (TextManager管理) を描画
        // FloatingTextSystemが登録したものはここに含まれる
        _textManager->Render(true);

        // 2D文字を描画するためのキャンバスを白紙にする
        _textManager->GetFontSprite()->Begin();

        // 2. UI全体を描画 (UIManager管理)
        // ここで画像も、UIText(お金)も、全部まとめて描画される
        _uiManager->Render();

        // 溜まった文字のキャンバスを、文字専用の設定で一気に画面に焼き付ける
        // 以前コメントアウトして消した「2D描画用のRender」を復活させるのが正解です
        auto *dc = Graphics::Instance().GetDeviceContext();
        _textManager->GetFontSprite()->Flush(dc);
    }
}

void ShopScene::OnRender3D(RenderContext &rc)
{
}

void ShopScene::OnDrawImGui()
{
    EditorContext context;
    context.selectedEntity = &selectedEntity;
    context.world          = _worldPtr;
    context.systemManager  = _ecsContext->GetSystemManager(); 

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

        ImGui::Begin("StageManager");
        auto &stageIndex = SceneManager::Instance().GetPlayerData().stageLevel;

        ImGui::SliderInt("StageLevel", &stageIndex, 1, 5);

        ImGui::End();
    }
}

void ShopScene::OnPreLoadScene()
{
    selectedEntity = 0;
}

// =========================================================================
// ★追加: 各State（カセット）の中身の実装
// =========================================================================

void ShopPlayingState::Update(float dt)
{
    // 1. ESCキーが押されたらポーズ状態へ切り替える
    //if (Input::IsKeyDown(Keys::Escape)) {
    //    _scene->ChangeToPausedState();
    //    return;
    //}
    // 2. プレイ中なので、世界（ECS）の時間を進める
    _scene->UpdateECS(dt);
}

void ShopPausedState::Enter()
{
    _scene->ShowPauseMenu(); // 状態に入った瞬間にメニューを出す
}

void ShopPausedState::Exit()
{
    _scene->HidePauseMenu(); // 状態を抜ける瞬間にメニューを消す
}

void ShopPausedState::Update(float dt)
{
    // 1. もう一度ESCキーが押されたらプレイ状態へ戻る
    //if (Input::IsKeyDown(Keys::Escape)) {
    //    _scene->ChangeToPlayingState();
    //    return;
    //}
    // ★注意: _scene->UpdateECS(dt) を「呼ばない」ことで、ゲーム内時間が完全に停止する
}

// 状態切り替えのヘルパー関数
void ShopScene::ChangeToPlayingState() { ChangeState(std::make_unique<ShopPlayingState>(this)); }
void ShopScene::ChangeToPausedState() { ChangeState(std::make_unique<ShopPausedState>(this)); }