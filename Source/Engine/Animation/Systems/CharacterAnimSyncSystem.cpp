/// @file CharacterAnimSyncSystem.cpp

#include "CharacterAnimSyncSystem.h"
#include "Engine/Animation/Components/RootMotionComponent.h" // externallyDriven（外部が移動を駆動中か）
#include "Engine/Core/Math/StringHash.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <SimpleMath.h>
#include <algorithm>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

// 毎フレームの HashString 計算を省くための事前計算（キャッシュ）
static const uint32_t HASH_SPEED       =  CCL::Utils::HashString("Speed");
static const uint32_t HASH_Y_VELOCITY  =  CCL::Utils::HashString("YVelocity");
static const uint32_t HASH_IS_GROUNDED =  CCL::Utils::HashString("IsGrounded");

// トリガー用（エッジ検出）
static const uint32_t HASH_TRIG_JUMP =  CCL::Utils::HashString("OnJump");
static const uint32_t HASH_TRIG_LAND =  CCL::Utils::HashString("OnLand");

void CharacterAnimSyncSystem::Update(float /*dt*/)
{
    ForEachWithID([&](EntityID                            id,
                      const JoltCharacterHandleComponent &joltHandle,
                      const JoltCharacterConfigComponent &config,
                      AnimParametersComponent            &animParams,
                      AnimatorComponent                  &animator) {
        if (!joltHandle.character)
            return;
        JPH::CharacterVirtual *ch = joltHandle.character.GetPtr();

        // ====================================================================
        // 1. 物理データの抽出
        // ====================================================================
        JPH::Vec3 linearVel = ch->GetLinearVelocity();
        Vector3   horizontalVel(linearVel.GetX(), 0.0f, linearVel.GetZ());

        float currentSpeed = horizontalVel.Length();
        float currentYVel  = linearVel.GetY();
        bool  isGrounded   = (ch->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround);

        // 前フレームの接地状態を読み取る（エッジ検出用）
        bool wasGrounded = animParams.GetBool(HASH_IS_GROUNDED);

        // 「外部（ゲーム側の移動アクション）が移動を駆動中か」を取得する。
        // ダッシュ等でゲーム側が速度を握っている間は足滑り補正を掛けない。
        // ※ ゲーム固有の DashComponent を直接読まず、Engine 側のこのフラグ経由で見る。
        //    PlayerDashSystem が rm->externallyDriven = dash.isDashing を毎フレーム設定する。
        bool externallyDriven = false;
        if (auto *rootMotion = _world->GetComponent<RootMotionComponent>(id))
        {
            externallyDriven = rootMotion->externallyDriven;
        }

        // ====================================================================
        // 2. 掲示板（AnimParameters）への書き込み
        // ====================================================================
        animParams.SetFloat(HASH_SPEED, currentSpeed);
        animParams.SetFloat(HASH_Y_VELOCITY, currentYVel);
        animParams.SetBool(HASH_IS_GROUNDED, isGrounded);
        // IsDashing はゲーム側(PlayerAnimParamSyncSystem)が同ステージで書き込む。

        // エッジ検出によるトリガー発行（ジャンプと着地）
        if (isGrounded && !wasGrounded)
        {
            animParams.SetTrigger(HASH_TRIG_LAND); // 着地した瞬間
        }
        else if (!isGrounded && wasGrounded)
        {
            // ★ AAAの挙動: 上方向への明確な速度がある場合のみ「ジャンプ」とみなす
            // それ以外（歩いて落ちた等）はトリガーを飛ばさない。
            // グラフ側の [IsGrounded == false] の条件によって自動的に Fall へ遷移させる。
            if (currentYVel > 0.1f)
            {
                animParams.SetTrigger(HASH_TRIG_JUMP); // ジャンプした瞬間
            }
        }

        // ====================================================================
        // 3. 【AAAの極意】フットスライディング（足滑り）防止
        // ====================================================================
        // 移動速度がConfigの基準速度(walkSpeed)より速い/遅い場合、
        // アニメーションの再生速度(playbackSpeed)を動的に伸縮させて足の接地点を床に噛み合わせる。

        if (isGrounded && currentSpeed > 0.1f && !externallyDriven)
        {
            // 歩き/走り中: 実際の移動速度 ÷ 基準歩行速度 = 再生倍率
            // (例: 速度が12なら、基準6の2倍速でアニメを再生し、足が滑るのを防ぐ)
            float speedRatio = currentSpeed / (std::max)(0.1f, config.walkSpeed);

            // 倍率が極端にならないようクランプ（0.5倍 ～ 2.5倍）
            speedRatio = std::clamp(speedRatio, 0.5f, 2.5f);

            // Animatorのベース再生速度を上書き（※逆再生などは考慮しない簡易版）
            animator.playbackSpeed = speedRatio;
        }
        else
        {
            // 停止時、空中、ダッシュ中などは等速(1.0)に戻す
            animator.playbackSpeed = 1.0f;
        }
    });
}

// 物理エンジン(L03)の計算結果を受け取り、アニメーション評価(L02)の「前」に実行したいが、
// ループ順の都合上、PreUpdateの最後か、Updateの先頭付近に配置する。
REGISTER_LOGIC_SYSTEM(CharacterAnimSyncSystem, Priority::LogicStage::L02_PreUpdate);