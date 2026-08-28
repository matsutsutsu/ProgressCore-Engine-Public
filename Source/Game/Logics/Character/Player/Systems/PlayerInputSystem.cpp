#include "PlayerInputSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Input/IInputAPI.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/App/GameFlowState.h" // エディタ操作中(F2=uiInteract)は入力を遮断する
#include "Engine/Physics/Components/Character/KnockbackComponent.h" // 被弾硬直中は入力を遮断する

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
    //   ※「早期 return」ではなく「中立値を書き込む」のが要点。return するだけだと直前の移動方向が
    //     コンポーネントに残り、動きっぱなしになる。
    //   プレイヤー入力を読むのは本システムだけなので、ここで取得を止めれば全操作が一括で止まる。
    //   カットシーン中(cinematic)も同じ経路で止める。導入カメラ演出の最中に
    //   プレイヤーが動くと、演出が終わった瞬間に想定外の位置にいることになる。
    bool blockGameInput = false;
    if (m_world->HasResource<GameFlowState>())
    {
        const auto &flow = m_world->GetResource<GameFlowState>();
        blockGameInput   = flow.uiInteract || flow.cinematic;
    }

    // ボタンの生の押下状態。遮断中も読み続けるのが要点で、prev* をここへ追随させることで
    // 遮断が解けた瞬間に「押しっぱなし」を立ち上がりと誤認して技が暴発するのを防ぐ。
    const bool rawAttack = inputAPI->GetAction("Attack");
    const bool rawJump   = inputAPI->GetAction("Jump");
    const bool rawDash   = inputAPI->GetAction("Dash");
    const bool rawCleave = inputAPI->GetAction("Cleave"); // 切り払い(右クリック)
    const bool rawLockOn = inputAPI->GetAction("LockOn"); // Rボタン等

    // 移動方向はカメラ空間ベースで合成する (TPS特有の処理)。水平化して再正規化。
    Vector3 moveDir = Vector3::Zero;
    if (!blockGameInput)
    {
        const float moveX = inputAPI->GetAxis("MoveX");
        const float moveY = inputAPI->GetAxis("MoveY");

        Vector3 camForward = mainCamera->GetFront();
        Vector3 camRight   = mainCamera->GetRight();
        camForward.y = 0.0f; camForward.Normalize();
        camRight.y   = 0.0f; camRight.Normalize();

        moveDir = (camRight * moveX) + (camForward * moveY);
        if (moveDir.LengthSquared() > 0.001f)
            moveDir.Normalize();
    }

    ForEachWithID([&](EntityID id, CharacterMovementComponent &movement, PlayerInputComponent &pin) {

        // 被弾硬直中は操作を受け付けない。KnockbackComponent は被弾するキャラに常設だが、
        // 万一付いていなくても「操作できないまま復帰できない」より「硬直しない」方が
        // 安全なので、アーキタイプ条件にはせず任意依存として引く。
        bool stunned = false;
        if (const auto *kb = m_world->GetComponent<KnockbackComponent>(id))
            stunned = kb->IsStunned();

        const bool blocked = blockGameInput || stunned;

        // 移動要求だけは「止める」ではなく「中立を書く」。書かずに抜けると直前の
        // 進行方向が残り、硬直中も歩き続ける。
        movement.desiredVelocity = blocked ? Vector3::Zero : moveDir;

        // 立ち上がり(前ステップは押されておらず、今は押されている)だけを意思として通す。
        //   ロジックは同じ描画フレーム内で複数回走ることがあり、そのあいだ生の押下状態は
        //   変化しない。ここで畳んでおかないと、1回の押下が複数回のアクションになる。
        //   遮断中も prev は raw に追随させる。追随を止めると、遮断が解けた瞬間に
        //   押しっぱなしのボタンが立ち上がり扱いになって技が暴発する。
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


// ステージ L01: 全ての物理・移動処理の「前」に入力を確定させる
REGISTER_LOGIC_SYSTEM(PlayerInputSystem, Priority::LogicStage::L01_Input);
