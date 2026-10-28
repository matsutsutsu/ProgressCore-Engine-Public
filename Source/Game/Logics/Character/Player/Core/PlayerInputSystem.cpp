#include "PlayerInputSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Input/IInputAPI.h"
#include "Engine/Graphics/SceneView/Camera.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/App/GameFlowState.h"                          // エディタ操作中・演出中は入力を止める
#include "Engine/Physics/Components/Character/KnockbackComponent.h" // 被弾の硬直中は入力を止める

#include <algorithm>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

// プレイヤーの入力を読み、移動の向きと、ボタンを押した瞬間を書く
void PlayerInputSystem::Update(float dt)
{
    // 処理フロー:
    // 1. ゲームの入力を止めるかを決める
    // 2. ボタンの押下状態と、移動の向きを読む
    // 3. プレイヤーごとに、移動の向きと押した瞬間を書く

    if (!m_world->HasResource<std::shared_ptr<IInputAPI>>()) return;
    if (!m_world->HasResource<Camera *>()) return;

    auto    inputAPI   = m_world->GetResource<std::shared_ptr<IInputAPI>>();
    Camera *mainCamera = m_world->GetResource<Camera *>();

    // ---------------------------------------------------------
    // 1. 入力を止めるか
    // ---------------------------------------------------------

    // エディタ操作中（F2 の uiInteract・シーケンサの編集中）は止める
    // ゲームを止めずに ImGui を触るので、クリックで技が出ないようにする

    // 演出中・決着後も止める
    // 導入演出の最中に動くと、演出の後に想定外の位置から始まる
    bool blockGameInput = false;
    if (m_world->HasResource<GameFlowState>())
    {
        const auto &flow = m_world->GetResource<GameFlowState>();
        blockGameInput   = flow.uiInteract || flow.cinematic || flow.matchOver;
    }

    // ---------------------------------------------------------
    // 2. ボタンと移動の向き
    // ---------------------------------------------------------

    // ボタンの押下状態は、入力を止めている間も読む（3. で prev* を合わせるため）
    const bool rawAttack = inputAPI->GetAction("Attack");
    const bool rawJump   = inputAPI->GetAction("Jump");
    const bool rawDash   = inputAPI->GetAction("Dash");
    const bool rawLockOn = inputAPI->GetAction("LockOn");

    // 移動の向きは、カメラの向きを基準に合成する（TPS）
    Vector3 moveDir       = Vector3::Zero;
    float   moveIntensity = 0.0f; // スティックの傾き（0〜1）
    if (!blockGameInput)
    {
        const float moveX = inputAPI->GetAxis("MoveX");
        const float moveY = inputAPI->GetAxis("MoveY");

        Vector3 camForward = mainCamera->GetFront();
        Vector3 camRight   = mainCamera->GetRight();
        camForward.y       = 0.0f;
        camForward.Normalize();
        camRight.y = 0.0f;
        camRight.Normalize();

        moveDir = (camRight * moveX) + (camForward * moveY);

        // 傾きは、正規化で長さを捨てる前に測る
        // キーボードは常に 1。デッドゾーンは GamePad 側で処理済み
        moveIntensity = std::clamp(moveDir.Length(), 0.0f, 1.0f);
        if (moveDir.LengthSquared() > 0.001f) moveDir.Normalize();
    }

    // ---------------------------------------------------------
    // 3. プレイヤーごとに書く
    // ---------------------------------------------------------

    ForEachWithID([&](EntityID id, CharacterMovementComponent &movement, PlayerInputComponent &pin) {
        // 被弾の硬直中も入力を止める
        // KnockbackComponent が無いキャラは硬直しない扱いにするので、
        // ForEach の条件にせず GetComponent で引く
        bool stunned = false;
        if (const auto *kb = m_world->GetComponent<KnockbackComponent>(id))
        {
            stunned = kb->IsStunned();
        }

        const bool blocked = blockGameInput || stunned;

        // 止めるときも、移動の向きは 0 を書く
        // 書かずに抜けると直前の向きが残り、硬直中も歩き続ける
        if (blocked)
        {
            movement.desiredVelocity = Vector3::Zero;
            movement.moveIntensity   = 0.0f;
        }
        else
        {
            movement.desiredVelocity = moveDir;
            movement.moveIntensity   = moveIntensity;
        }

        // 前のステップで離していて、今押しているときだけ true にする
        // ロジックは1描画フレームに最大4回走り、その間は押下状態が変わらない。
        // ここで1回にまとめないと、1回の押下が複数回の技になる

        // 入力を止めている間も、prev は実際の押下状態に合わせる
        // 合わせないと、入力が戻った瞬間に押しっぱなしのボタンで技が出る
        auto edge = [blocked](bool raw, bool &prev) {
            const bool pressed = !blocked && raw && !prev;
            prev               = raw;
            return pressed;
        };

        pin.attackPressed = edge(rawAttack, pin.prevAttack);
        pin.jumpPressed   = edge(rawJump, pin.prevJump);
        pin.dashPressed   = edge(rawDash, pin.prevDash);
        pin.lockOnPressed = edge(rawLockOn, pin.prevLockOn);
    });
}



// 移動や物理より前に、入力を確定させる
REGISTER_LOGIC_SYSTEM(PlayerInputSystem, Priority::LogicStage::L01_Input);
