#include "CharacterAnimSyncSystem.h"
#include "Engine/Animation/Components/RootMotionComponent.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <SimpleMath.h>
#include <algorithm>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

// --- パラメータ名のハッシュ（毎ステップ計算しないよう先に求めておく）---
static const uint32_t HASH_SPEED       = CCL::Utils::HashString("Speed");
static const uint32_t HASH_Y_VELOCITY  = CCL::Utils::HashString("VelocityY");
static const uint32_t HASH_IS_GROUNDED = CCL::Utils::HashString("IsGrounded");

// --- 接地状態の変化で立てるトリガー ---
static const uint32_t HASH_TRIG_JUMP = CCL::Utils::HashString("Jump");
static const uint32_t HASH_TRIG_LAND = CCL::Utils::HashString("Land");



void CharacterAnimSyncSystem::Update(float /*dt*/)
{
    // 処理フロー:
    // 1. 物理の状態を取り出す
    // 2. パラメータとトリガーを書く
    // 3. 足滑り防止の再生速度倍率を書く

    ForEachWithID([&](EntityID                            id,
                      const JoltCharacterHandleComponent &joltHandle,
                      const JoltCharacterConfigComponent &config,
                      AnimParametersComponent            &animParams,
                      AnimatorComponent                  &animator) {
        JPH::CharacterVirtual *ch = joltHandle.character.GetPtr();

        // ---------------------------------------------------------
        // 1. 物理の状態
        // ---------------------------------------------------------
        JPH::Vec3 linearVel = ch->GetLinearVelocity();
        Vector3   horizontalVel(linearVel.GetX(), 0.0f, linearVel.GetZ());

        float currentSpeed = horizontalVel.Length();
        float currentYVel  = linearVel.GetY();
        bool  isGrounded   = (ch->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround);

        // 前のステップの接地状態（変化の検出用）
        bool wasGrounded = animParams.GetBool(HASH_IS_GROUNDED);

        // ゲーム側の移動アクション（ダッシュ等）が速度を制御中なら、足滑り補正を掛けない
        //   ゲーム固有の PlayerDashComponent を読まず、Engine 側の externallyDriven を見る
        bool externallyDriven = false;
        if (auto *rootMotion = m_world->GetComponent<RootMotionComponent>(id))
        {
            externallyDriven = rootMotion->externallyDriven;
        }

        // ---------------------------------------------------------
        // 2. パラメータとトリガー
        // ---------------------------------------------------------
        animParams.SetFloat(HASH_SPEED, currentSpeed);
        animParams.SetFloat(HASH_Y_VELOCITY, currentYVel);
        animParams.SetBool(HASH_IS_GROUNDED, isGrounded);

        // 接地状態が変わった瞬間にトリガーを立てる
        if (isGrounded && !wasGrounded)
        {
            animParams.SetTrigger(HASH_TRIG_LAND);
        }
        else if (!isGrounded && wasGrounded)
        {
            // 上向きの速度があるときだけジャンプとみなす
            //   歩いて落ちた場合はトリガーを立てず、グラフ側の条件（落下速度と空中の bool）で落下へ遷移させる
            if (currentYVel > 0.1f)
            {
                animParams.SetTrigger(HASH_TRIG_JUMP);
            }
        }

        // ---------------------------------------------------------
        // 3. 足滑りの防止
        // ---------------------------------------------------------

        // 移動速度と基準速度（walkSpeed）の比で再生速度を伸縮させ、足の接地を床に合わせる
        //   書き込み先は playbackSpeed ではなく footSlideMultiplier（理由は AnimatorComponent.h）

        // ループするクリップ（ロコモーション）にだけ掛ける
        //   非ループ（攻撃など）に掛けると、走行中の速度で攻撃アニメまで最大 2.5 倍速になる

        // ムービーがクリップを握っている間も掛けない（演出のクリップは指定した速度で見せる）
        if (animator.isLoop && isGrounded && currentSpeed > 0.1f && !externallyDriven &&
            !animator.isExternalOverride)
        {
            // 再生倍率 = 実際の移動速度 ÷ 基準の歩行速度（例: 速度 12、基準 6 なら 2 倍速）
            float speedRatio = currentSpeed / (std::max)(0.1f, config.walkSpeed);

            // 極端にならないよう 0.5〜2.5 倍に収める
            animator.footSlideMultiplier = std::clamp(speedRatio, 0.5f, 2.5f);
        }
        else
        {
            // 倍率は毎ステップ必ず代入する
            //   書かない経路があると、ロコモーションの倍率が攻撃へ持ち越される
            animator.footSlideMultiplier = 1.0f;
        }
    });
}



// アニメーションの遷移評価（L02_Update の AnimControllerSystem）より前に置く
//   読むのは前のステップの物理（L04_Physics）の結果
REGISTER_LOGIC_SYSTEM(CharacterAnimSyncSystem, Priority::LogicStage::L02_PreUpdate);
