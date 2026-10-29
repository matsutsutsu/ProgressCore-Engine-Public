#pragma once
// ===========================================================================
// 概要: C++ で書く攻撃陣形。データ（Assets/Formations/*.json）で書けない技だけを置く。
//
// 責務:
//   - 弾幕（Barrage）: 頭上の輪から1機ずつ中心へ寄り、プレイヤーへ撃ち出す
//
// 備考:
//   データ陣形は「全機に同じ進行度を配って形を動かす」だけなので、機体ごとに状態を持つ技は書けない。
//   弾幕は機体ごとに発射の順番を持ち、撃つ瞬間まで狙い直し、撃った後は各機が独立して攻撃を終える。
// ===========================================================================
#include "Game/Logics/AI/Drone/Components/DroneTuning.h" // 射出速度（kDroneChargeSpeedScale）
#include "Game/Logics/AI/Drone/Formation/IFormation.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include "Game/Logics/AI/Drone/Formation/Formations_Basic.h" // 弾幕が頭上リングを借りる
#include <cmath>
#include <algorithm>

namespace CCL::AI::Formation::Attack
{
    /**
     * @brief 弾幕（Barrage）。求める回避はジャスト回避。
     *
     * 輪の上で待つ → 自分の番の少し前に中心へ寄る（これが予告）→ 中心でタメ → 発射（以降は直進）。
     * 発射の瞬間まで1機ずつ今のプレイヤーを狙い続け、撃った後は直進するので避ける余地が残る。
     */
    class BarrageFormation : public IFormation
    {
    public:
        // 撃った機から順に帰す。経過時間は、壁際などで飛距離が伸びないときの保険
        bool IsChargeOver(float flownDistance, float flownTime) const override
        {
            return flownDistance > kChargeRange || flownTime > kChargeTimeout;
        }

        // 中心へ集まる動きが予告なので、震えを足すと二重になる
        bool UsesShiverTelegraph() const override { return false; }

        // 残すと、まだ撃っていなかった機が次の技で発射待ちのまま当たり判定を開かない
        void OnExit(DroneComponent &drone) const override { drone.fireDelay = 0.0f; }

        // 1機ずつ順に撃つので、撃ってから戻すまでを長く取る
        bool TryGetTiming(TechniqueTiming &out) const override
        {
            out = {1.2f, 4.2f, 3.0f};
            return true;
        }

        void Calculate(DroneComponent &drone, const FormationContext &ctx) override
        {
            using namespace DirectX::SimpleMath;

            // 処理フロー:
            // 1. 飛び出した後の機には何も書かない
            // 2. 持ち場を決める（輪の席から発射地点へ寄せる）
            // 3. 偏差込みで狙いを決める
            // 4. 攻撃に入る前だけ発射順を配る

            // --- 飛び出した後 ---

            // 順番待ちとタメの間はまだ撃っていないので、狙いを更新し続ける。
            // タメを含め忘れると古い座標を撃ち、歩いているだけで当たらなくなる
            const bool launched = (drone.currentState == DroneState::FireCharge) &&
                                  (drone.fireDelay <= 0.0f) && (drone.chargeWindup <= 0.0f);
            if (launched) return;

            drone.chargeMode = ChargeMode::Straight;

            const int total = (drone.totalDrones > 0) ? drone.totalDrones : 1;

            // --- 持ち場 ---

            // 待機位置は頭上円陣と同じ輪
            const Vector3 slot = ctx.bossPos + Basic::HighRing::SlotOffset(
                                                   drone.localIndex, drone.totalDrones, ctx.time);

            const Vector3 muzzle =
                ctx.bossPos + Vector3::UnitY * (Basic::HighRing::kHeight - kGatherDrop);

            // 自分の番が近づくほど中心へ寄る。二乗にして直前に素早く寄せる（線形だといつ撃つのか読めない）
            float gather = 0.0f;
            if (drone.currentState == DroneState::FireCharge && drone.fireDelay <= 0.0f)
            {
                // タメ中は発射地点のまま。輪へ戻すと、下の先読みが実際の発射地点とずれる
                gather = 1.0f;
            }
            else if (drone.fireDelay > 0.0f && drone.fireDelay < kGatherTime)
            {
                const float t = 1.0f - (drone.fireDelay / kGatherTime);
                gather        = t * t;
            }

            drone.targetPosition = Vector3::Lerp(slot, muzzle, gather);

            // --- 狙い ---

            // 偏差射撃。狙う先 ＝ 今の位置 ＋ 速度 × (タメの残り ＋ 距離÷射出速度)。
            // 距離が狙う先に依存する循環なので、1回だけ計算し直して精度を上げる
            const float launchSpeed = drone.moveSpeed * kDroneChargeSpeedScale;
            const float windupLeft  = (drone.chargeWindup > 0.0f) ? drone.chargeWindup : 0.0f;

            Vector3 aimPoint = ctx.playerCenter;
            if (launchSpeed > 0.001f)
            {
                for (int i = 0; i < 2; ++i)
                {
                    const float flight =
                        (aimPoint - drone.targetPosition).Length() / launchSpeed;
                    aimPoint = ctx.playerCenter +
                               ctx.playerVelocity * ((windupLeft + flight) * kLeadFactor);
                }
            }

            Vector3 toAim = aimPoint - drone.targetPosition;
            if (toAim.LengthSquared() > 0.001f)
                toAim.Normalize();
            else
                toAim = Vector3::UnitZ;

            drone.fireDirection       = toAim;
            drone.targetLookDirection = toAim;

            // --- 発射順 ---

            // localIndex が小さい機ほど早い。攻撃に入る前だけ配る（後で書き直すと順番が戻り続けて撃てない）
            if (drone.currentState != DroneState::FireCharge)
            {
                drone.fireDelay =
                    (static_cast<float>(drone.localIndex) / static_cast<float>(total)) * kFireWindow;
            }
        }

        static constexpr float kFireWindow = 8.0f; // 全機を撃ち切る秒数。伸ばすほど発射レートが下がる
        static constexpr float kGatherTime = 0.6f; // 発射の何秒前から中心へ寄り始めるか
        static constexpr float kGatherDrop = 6.0f; // 輪の高さから何m下を発射地点にするか

        static constexpr float kLeadFactor = 0.5f; // 先読みの強さ。0=今の位置 / 1=完全な迎撃。歩くだけでは避けられない値にする

        // 突撃を終える飛距離 [m]。短いと届く前に判定が閉じ、重なっているのに当たらない。
        // BT の発動距離（水平 120m）と発射地点の高さから逆算している。発動距離を伸ばすならここも伸ばす
        static constexpr float kChargeRange = 140.0f;

        static constexpr float kChargeTimeout = 2.0f; // 突撃の最長時間 [秒]
    };

    inline void RegisterAll(FormationRegistry &reg)
    {
        reg.Register<BarrageFormation>(DroneFormationType::Barrage);
    }

} // namespace CCL::AI::Formation::Attack
