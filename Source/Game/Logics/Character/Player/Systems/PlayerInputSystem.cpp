#include "PlayerInputSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Input/IInputAPI.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/Core/Math/ScreenToWorld.h"

// ※ 実装ファイル内でのみ展開する
using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

PlayerInputSystem::PlayerInputSystem()
    : IfSystem("PlayerInputSystem")
{
}

void PlayerInputSystem::Update(float dt)
{
    if (!_world->HasResource<std::shared_ptr<IInputAPI>>()) return;
    if (!_world->HasResource<Camera*>()) return;

    auto inputAPI = _world->GetResource<std::shared_ptr<IInputAPI>>();
    Camera* mainCamera = _world->GetResource<Camera*>();

    // 1. デバイスからの入力取得
    float moveX       = inputAPI->GetAxis("MoveX");
    float moveY       = inputAPI->GetAxis("MoveY");
    bool  isAttacking = inputAPI->GetAction("Attack");
    bool  isJumping   = inputAPI->GetAction("Jump");
    bool  isDashing   = inputAPI->GetAction("Dash"); // ダッシュ入力の取得

    // 2. カメラ空間ベースのベクトル生成 (TPS特有の処理)
    // ※Cameraクラスの仕様に合わせて GetForward() 等の関数名は適宜読み替えること
    Vector3 camForward = mainCamera->GetFront();
    Vector3 camRight = mainCamera->GetRight();

    // 水平移動にするためY軸の傾きを潰し、再正規化する
    camForward.y = 0.0f;
    camRight.y = 0.0f;
    camForward.Normalize();
    camRight.Normalize();

    // 3. 入力値とカメラベクトルを合成してワールド進行方向を決定
    Vector3 moveDir = (camRight * moveX) + (camForward * moveY);
    if (moveDir.LengthSquared() > 0.001f) {
        moveDir.Normalize();
    }

    // 4. Chunk一括更新 (SoA)
    ForEach([&](CharacterMovementInputComponent& inputData, const PlayerComponent& player) {

        // 要求速度、攻撃意思、ジャンプ意思の書き込み
        inputData.desiredVelocity   = moveDir;
        inputData.isAttackRequested = isAttacking;
        inputData.jumpRequested     = isJumping;
        inputData.dashRequested     = isDashing; // ダッシュ要求をコンポーネントへ渡す

        // TPSにおけるエイム先は「カメラの中心」となるため、マウス交差計算は撤廃。
        // 必要に応じてカメラの RayCast などの結果をここに流し込む設計とする。
        });
}


// ステージ L01: 全ての物理・移動処理の「前」に入力を確定させる
REGISTER_LOGIC_SYSTEM(PlayerInputSystem, Priority::LogicStage::L01_Input);