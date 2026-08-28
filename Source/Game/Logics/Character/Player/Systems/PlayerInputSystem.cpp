#include "PlayerInputSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Input/IInputAPI.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/App/GameFlowState.h" // エディタ操作中(F2=uiInteract)は入力を遮断する

#include "Engine/Core/Math/ScreenToWorld.h"


using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

PlayerInputSystem::PlayerInputSystem()
    : IfSystem("PlayerInputSystem")
{
}

void PlayerInputSystem::Update(float dt)
{
    if (!m_world->HasResource<std::shared_ptr<IInputAPI>>()) return;
    if (!m_world->HasResource<Camera*>()) return;

    auto inputAPI = m_world->GetResource<std::shared_ptr<IInputAPI>>();
    Camera* mainCamera = m_world->GetResource<Camera*>();

    // エディタ操作中(F2でカーソル自由=uiInteract / シーケンサEditMode)は、ゲームを止めずに
    // カーソルだけ解放して ImGui を触れる。この間は全ゲーム入力を無視する（右クリックの技暴発防止）。
    //   ※「早期 return」ではなく「中立値を書き込む」のが要点。return するだけだと直前の移動方向や
    //     cleaveRequested がコンポーネントに残り、動きっぱなし／技が押されっぱなしで暴発し続ける。
    //   プレイヤー入力を読むのは本システムだけなので、ここで取得を止めれば全操作が一括で止まる。
    //   カットシーン中(cinematic)も同じ経路で止める。導入カメラ演出の最中に
    //   プレイヤーが動くと、演出が終わった瞬間に想定外の位置にいることになる。
    bool blockGameInput = false;
    if (m_world->HasResource<GameFlowState>())
    {
        const auto &flow = m_world->GetResource<GameFlowState>();
        blockGameInput   = flow.uiInteract || flow.cinematic;
    }

    // 既定は「入力なし(中立)」。ブロック中はこの中立値のまま下の ForEach で書き込む＝停止。
    Vector3 moveDir     = Vector3::Zero;
    bool    isAttacking = false;
    bool    isCleaving  = false;
    bool    isJumping   = false;
    bool    isDashing   = false;
    bool    isLockOn    = false;

    if (!blockGameInput)
    {
        // 1. デバイスからの入力取得
        const float moveX = inputAPI->GetAxis("MoveX");
        const float moveY = inputAPI->GetAxis("MoveY");
        isAttacking = inputAPI->GetAction("Attack");
        isCleaving  = inputAPI->GetAction("Cleave"); // 切り払い(右クリック)
        isJumping   = inputAPI->GetAction("Jump");
        isDashing   = inputAPI->GetAction("Dash");   // ダッシュ
        isLockOn    = inputAPI->GetAction("LockOn"); // Rボタン等

        // 2. カメラ空間ベースで進行方向を合成 (TPS特有の処理)。水平化して再正規化。
        Vector3 camForward = mainCamera->GetFront();
        Vector3 camRight   = mainCamera->GetRight();
        camForward.y = 0.0f; camForward.Normalize();
        camRight.y   = 0.0f; camRight.Normalize();

        moveDir = (camRight * moveX) + (camForward * moveY);
        if (moveDir.LengthSquared() > 0.001f)
            moveDir.Normalize();
    }

    // 3. コンポーネントへ確定書き込み（ブロック中は全て中立＝停止・未入力）
    ForEach([&](CharacterMovementInputComponent& inputData, PlayerComponent& player) {

        // 要求速度、攻撃意思、ジャンプ意思の書き込み
        inputData.desiredVelocity   = moveDir;
        inputData.isAttackRequested = isAttacking;
        inputData.jumpRequested     = isJumping;
        inputData.dashRequested     = isDashing; // ダッシュ要求をコンポーネントへ渡す
        player.cleaveRequested       = isCleaving; // 切り払い(右クリック)はプレイヤー固有意思へ
        player.lockOnToggleRequested = isLockOn;  // ロックオン要求をコンポーネントへ渡す

    });
}


// ステージ L01: 全ての物理・移動処理の「前」に入力を確定させる
REGISTER_LOGIC_SYSTEM(PlayerInputSystem, Priority::LogicStage::L01_Input);
