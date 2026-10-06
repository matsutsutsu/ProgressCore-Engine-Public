#include "PlayerInputSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Input/IInputAPI.h"
#include "Engine/Graphics/SceneView/Camera.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/App/GameFlowState.h"                          // エディタ操作中(F2=uiInteract)は入力を止める
#include "Engine/Physics/Components/Character/KnockbackComponent.h" // 被弾の硬直中は入力を止める

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

PlayerInputSystem::PlayerInputSystem()
    : IfSystem("PlayerInputSystem")
{
}

void PlayerInputSystem::Update(float dt)
{
    if (!m_world->HasResource<std::shared_ptr<IInputAPI>>()) return;
    if (!m_world->HasResource<Camera *>()) return;

    auto    inputAPI   = m_world->GetResource<std::shared_ptr<IInputAPI>>();
    Camera *mainCamera = m_world->GetResource<Camera *>();

    // エディタ操作中（F2 でカーソルを解放した uiInteract / シーケンサの EditMode）は、ゲームを止めずに
    // ImGui を操作できる。この間はゲームの入力をすべて無視する（右クリックで技が出ないように）。
    //   早期 return ではなく中立の値を書くこと。return するだけだと直前の移動方向が
    //   Component に残り、動き続ける。
    //   プレイヤーの入力を読むのはこの System だけなので、ここで止めれば全操作がまとめて止まる。
    //   カットシーン中(cinematic)も同じ方法で止める。導入演出の最中にプレイヤーが動くと、
    //   演出が終わった瞬間に想定外の位置にいることになる。
    bool blockGameInput = false;
    if (m_world->HasResource<GameFlowState>())
    {
        const auto &flow = m_world->GetResource<GameFlowState>();
        blockGameInput   = flow.uiInteract || flow.cinematic;
    }

    // ボタンの実際の押下状態。入力を止めている間も読み続け、prev* に反映することで、
    // 入力が戻った瞬間に押しっぱなしのボタンを「押した瞬間」と判定して技が出るのを防ぐ。
    const bool rawAttack = inputAPI->GetAction("Attack");
    const bool rawJump   = inputAPI->GetAction("Jump");
    const bool rawDash   = inputAPI->GetAction("Dash");
    const bool rawCleave = inputAPI->GetAction("Cleave"); // 切り払い(右クリック)
    const bool rawLockOn = inputAPI->GetAction("LockOn"); // Rキーなど

    // 移動方向はカメラの向きを基準に合成する（TPS）。水平にしてから正規化する。
    Vector3 moveDir = Vector3::Zero;
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
        if (moveDir.LengthSquared() > 0.001f)
            moveDir.Normalize();
    }

    ForEachWithID([&](EntityID id, CharacterMovementComponent &movement, PlayerInputComponent &pin) {
        // 被弾の硬直中は操作を受け付けない。KnockbackComponent は被弾するキャラには常に付いているが、
        // 付いていない場合は「硬直しない」で済ませたいので、Archetype の条件にせず GetComponent で引く。
        bool stunned = false;
        if (const auto *kb = m_world->GetComponent<KnockbackComponent>(id))
            stunned = kb->IsStunned();

        const bool blocked = blockGameInput || stunned;

        // 移動の要求は、止めるときも中立（0）を書く。書かずに抜けると直前の
        // 移動方向が残り、硬直中も歩き続ける。
        movement.desiredVelocity = blocked ? Vector3::Zero : moveDir;

        // 前のステップでは押されておらず、今は押されているときだけ true にする。
        //   ロジックは同じ描画フレーム内で複数回走ることがあり、その間は実際の押下状態が
        //   変わらない。ここで1回にまとめないと、1回の押下が複数回のアクションになる。
        //   入力を止めている間も prev は raw に合わせる。
        auto edge = [blocked](bool raw, bool &prev) {
            const bool pressed = !blocked && raw && !prev;
            prev               = raw;
            return pressed;
        };

        pin.attackPressed = edge(rawAttack, pin.prevAttack);
        pin.jumpPressed   = edge(rawJump, pin.prevJump);
        pin.dashPressed   = edge(rawDash, pin.prevDash);
        pin.cleavePressed = edge(rawCleave, pin.prevCleave);
        pin.lockOnPressed = edge(rawLockOn, pin.prevLockOn);
    });
}

// L01_Input: 移動や物理の処理より前に入力を確定させる
REGISTER_LOGIC_SYSTEM(PlayerInputSystem, Priority::LogicStage::L01_Input);
