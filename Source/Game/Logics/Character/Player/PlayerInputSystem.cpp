#include "PlayerInputSystem.h"

#include "ECS/Core/CCL_Chunk.h"
#include "Engine/Graphics/Core/Camera.h"
#include "Engine/Graphics/Core/Graphics.h"
#include "Engine/Platform/Input/Input.h"
#include <SimpleMath.h>
#include <imgui.h>

// システムの実行順序の定義ヘッダー
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

// 必要に応じてWindows.hをインクルードしてください
// #define NOMINMAX
// #include <Windows.h>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath; // Vector3, Matrix等を使用
using namespace DirectX;             // Vector3, Matrix等を使用

PlayerInputSystem::PlayerInputSystem()
    : IfSystem("PlayerInputSystem")
{
}

void PlayerInputSystem::Update(float dt)
{
    // --- 追加: ゲーム状態のチェック ---
    if (_world->HasResource<GameState>()) {
        const auto &state = _world->GetResource<GameState>();

        // ゲームプレイ中以外は入力をゼロにして強制リターン（またはゼロ埋め処理）
        if (state.mode != GameStateMode::Gameplay) {
            ForEach([&](PlayerComponent &players) {
                players.input.moveDir = {0, 0, 0};
                players.input.jump    = false;
                players.input.attack  = false;
                // lookPosは更新しない、またはプレイヤーの正面を入れておく
            });
            return;
        }
    }

    Camera *mainCamera = nullptr;
    if (_world->HasResource<Camera *>()) {
        mainCamera = _world->GetResource<Camera *>();
    }

    // シングルトンからハードウェア情報を取得
    auto &inputManager = Input::Instance();
    auto &pad          = inputManager.GetGamePad();
    auto &mouse        = inputManager.GetMouse();

    // Logic用の蓄積入力を取得
    unsigned int logicPadDown   = inputManager.GetPadButtonDownLogic();
    unsigned int logicMouseDown = inputManager.GetMouseButtonDownLogic();

    ImGuiIO &io         = ImGui::GetIO();
    bool     allowMouse = !io.WantCaptureMouse;

    // --- マウスレイキャストの準備 ---
    Vector3 mouseWorldPos = Vector3::Zero;
    bool    hasMouseInput = false;

    if (mainCamera && allowMouse) {
        // 1. マウス位置と画面サイズを取得

        // スクリーンサイズ取得
        float screenWidth  = Graphics::Instance().GetScreenWidth();
        float screenHeight = Graphics::Instance().GetScreenHeight();

        // マウスカーソル位置の取得
        POINT cursor;
        ::GetCursorPos(&cursor);
        ::ScreenToClient(Graphics::Instance().GetWindowHandle(), &cursor);

        // スクリーン座標の設定
        XMFLOAT3 screenPosition;
        screenPosition.x = static_cast<float>(cursor.x);
        screenPosition.y = static_cast<float>(cursor.y);

        // 2. カメラ行列を取得
        Matrix view  = mainCamera->GetView();
        Matrix proj  = mainCamera->GetProjection();
        Matrix world = Matrix::Identity; // 世界の基準

        // 1. Vector3 を XMVECTOR に変換
        XMVECTOR vNearSource = XMVectorSet(screenPosition.x, screenPosition.y, 0.0f, 0.0f);
        XMVECTOR vFarSource  = XMVectorSet(screenPosition.x, screenPosition.y, 1.0f, 0.0f);

        // 2. Unproject 実行 (SimpleMathのMatrixは自動的にXMMATRIXとして扱われます)
        XMVECTOR vNear = XMVector3Unproject(
            vNearSource, 0, 0, screenWidth, screenHeight, 0.0f, 1.0f, proj, view, world);

        XMVECTOR vFar = XMVector3Unproject(
            vFarSource, 0, 0, screenWidth, screenHeight, 0.0f, 1.0f, proj, view, world);

        // 3. 結果を Vector3 に戻す
        Vector3 nearPoint = vNear;
        Vector3 farPoint  = vFar;

        // 4. レイを作成
        Vector3 rayDir = farPoint - nearPoint;
        rayDir.Normalize();

        // 5. 地面（Y=0平面）との交差判定
        // プレイヤーの足元がY=0でない場合は、Planeの高さ(d)を調整してください
        Plane groundPlane(Vector3::Up, 0.0f);
        Ray   ray(nearPoint, rayDir);

        float dist;
        if (ray.Intersects(groundPlane, dist)) {
            mouseWorldPos = nearPoint + rayDir * dist;
            hasMouseInput = true;
        }
    }

    ForEach([&](PlayerComponent &players) {
        // --- ★修正: 移動入力の同時押し対策 ---
        float moveX = 0.0f;
        float moveZ = 0.0f;

        // キーボードの状態を直接取得 (0x8000は押されているかのビットフラグ)
        // Windowsの仮想キーコード: 'A'=0x41, 'D'=0x44, 'W'=0x57, 'S'=0x53
        bool keyA = (GetAsyncKeyState('A') & 0x8000) != 0;
        bool keyD = (GetAsyncKeyState('D') & 0x8000) != 0;
        bool keyW = (GetAsyncKeyState('W') & 0x8000) != 0;
        bool keyS = (GetAsyncKeyState('S') & 0x8000) != 0;

        // X軸 (AとD)
        if (keyA && keyD) {
            moveX = 0.0f; // 両方押されていたら相殺して0にする
        }
        else if (keyA) {
            moveX = -1.0f;
        }
        else if (keyD) {
            moveX = 1.0f;
        }
        else {
            // キーボード入力がない場合は、パッド(スティック)の値を採用
            moveX = pad.GetAxisLX();
        }

        // Z軸 (WとS) も同様に処理
        if (keyW && keyS) {
            moveZ = 0.0f;
        }
        else if (keyW) {
            moveZ = 1.0f; // 前
        }
        else if (keyS) {
            moveZ = -1.0f; // 後ろ
        }
        else {
            moveZ = pad.GetAxisLY();
        }

        // 生入力を「ゲーム上の意味（アクション）」にマッピング
        // ここを書き換えるだけでキーコンフィグやマウス対応が可能になる
        players.input.moveDir.x = moveX;
        players.input.moveDir.y = 0.0f;
        players.input.moveDir.z = moveZ;

        players.input.jump = (logicPadDown & GamePad::BTN_A);

        // マウス位置をComponentに反映
        if (hasMouseInput) {
            players.input.lookPos = mouseWorldPos;
        }
        else {
            // マウス判定が失敗した場合（空を見ている等）、移動方向などを見るか、前回の値を維持
            // ここでは便宜上、自分の少し前を入れておく等のフォールバックが可能
        }

        // ImGuiにマウスが重なっていたら
        if (allowMouse) {

            // 「押した瞬間（Down）」ではなく「押している間ずっと（IsDown）」を取得する
            bool isPadShooting   = pad.IsDown(GamePad::BTN_RIGHT_TRIGGER);
            bool isMouseShooting = mouse.IsDown(Mouse::BTN_LEFT);

            players.input.attack = (isPadShooting || isMouseShooting);
        }
        else {
            players.input.attack = false;
        }
    });
}

REGISTER_LOGIC_SYSTEM(PlayerInputSystem, Priority::LogicStage::Input);