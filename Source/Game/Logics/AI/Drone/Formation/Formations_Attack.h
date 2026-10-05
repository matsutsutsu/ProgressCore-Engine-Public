#pragma once
// ===========================================================================
// 概要: C++ で書く攻撃陣形。データ（Assets/Formations/*.json）で書けない技だけを置く。
//
// 責務:
//   - 弾幕（Barrage）: 頭上の輪から1機ずつ中心へ寄り、プレイヤーへ撃ち出す
//
// 備考:
//   データで書けないのは、機体が自分の状態を覚えて振る舞いを変える技。弾幕は機体ごとに
//   発射の順番(fireDelay)を持ち、撃つ瞬間まで先読みで狙い直し、撃った後は各機が
//   独立して攻撃を終える。データ陣形は「全機に同じ進行度を配って形を動かす」だけなので、
//   この3つはどれも表せない。
//
//   縦割り・挟み込み・横なぎは Assets/Formations/*.json にある
//   （Data/DataDrivenFormation.h が解釈する）。
// ===========================================================================
#include "Game/Logics/AI/Drone/Components/DroneTuning.h" // 射出速度（kDroneChargeSpeedScale）
#include "Game/Logics/AI/Drone/Formation/IFormation.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include "Game/Logics/AI/Drone/Formation/Formations_Basic.h" // 弾幕が頭上リングを借りる
#include <cmath>
#include <algorithm>

namespace CCL::AI::Formation::Attack
{
    // 弾幕（Barrage）。求める回避はジャスト回避。
    // 頭上の輪を回したまま1機ずつ中心へ降り、そこからプレイヤーへ高速で撃ち出す。
    //   輪の上で待つ → 自分の番の kGatherTime 秒前に中心へ寄る（これが予告）
    //   → fireDelay が 0 になったら中心でタメ → 発射（以降は直進）
    //
    // 置き技（縦割り・挟み込み）と違い、発射の瞬間まで1機ずつ今のプレイヤーを狙い続ける。
    // 撃ち出した後は直進するので、そこで避ける余地が残る。
    struct BarrageFormation : public IFormation
    {
        // 撃ち出したら各機が独立して飛ぶので、終了判断は個体に持たせる（帰投の時刻が機体ごとに異なる）。
        ChargeLifecycle GetLifecycle() const override { return ChargeLifecycle::Individual; }

        // 中心へ集まる移動そのものが予告。震えを足すと予告が二重になり、どちらを読むか分かりにくくなる。
        bool UsesShiverTelegraph() const override { return false; }

        void Calculate(DroneComponent &drone, const FormationContext &ctx) override
        {
            using namespace DirectX::SimpleMath;

            // 処理フロー:
            // 1. 飛び出した後の機には何も書かない
            // 2. 持ち場を決める（輪の席から発射地点へ寄せる）
            // 3. 偏差込みで狙いを決める
            // 4. 攻撃に入る前だけ発射順を配る

            // --- 飛び出した後 ---

            // 陣形が持ち場を書かなくなるのは「飛び出した後」だけ。
            // 順番待ちも中央でのタメもまだ撃っていないので、狙いを更新し続ける。

            // 判定に stateTimer（タメ）を含めること。
            // 見落とすと着弾の 0.5 秒以上前の座標を撃つことになり、歩いているだけで当たらなくなる。
            const bool launched = (drone.currentState == DroneState::FireCharge) &&
                                  (drone.fireDelay <= 0.0f) && (drone.stateTimer <= 0.0f);
            if (launched) return;

            drone.chargeMode = ChargeMode::Straight;

            const int total = (drone.totalDrones > 0) ? drone.totalDrones : 1;

            // --- 持ち場 ---

            // 待機位置は頭上円陣と同じ輪。
            // 回転位相まで共有するので、BT が 頭上円陣 → 弾幕 と切り替えてもドローンは動かない。
            const Vector3 slot = ctx.bossPos + Basic::HighRing::SlotOffset(
                                                   drone.localIndex, drone.totalDrones, ctx.time);

            // 発射地点は輪の中心の少し下。
            const Vector3 muzzle =
                ctx.bossPos + Vector3::UnitY * (Basic::HighRing::kHeight - kGatherDrop);

            // 自分の番が近づくほど中心へ寄る。

            // 寄せ方は二乗にする。
            // 序盤をほぼ動かさず、直前に素早く中心へ寄る形にするため（線形だといつ撃つのか読めない）。
            float gather = 0.0f;
            if (drone.currentState == DroneState::FireCharge && drone.fireDelay <= 0.0f)
            {
                // 中央でのタメ中は持ち場を発射地点のままにする。
                // 輪へ戻すと下の飛翔時間が「輪から撃つ」前提になり、実際の発射地点とずれた先読みになる。
                gather = 1.0f;
            }
            else if (drone.fireDelay > 0.0f && drone.fireDelay < kGatherTime)
            {
                const float t = 1.0f - (drone.fireDelay / kGatherTime);
                gather        = t * t;
            }

            drone.targetPosition = Vector3::Lerp(slot, muzzle, gather);

            // --- 狙い ---

            // 偏差射撃。今いる場所を撃つと、飛翔時間ぶん必ず後ろへ外れる。
            // そのため 狙う先 ＝ 今の位置 ＋ 速度 × (タメの残り ＋ 距離÷射出速度) を解く。

            // 距離が狙う先に依存する循環なので、1回だけ計算し直して精度を上げる。
            const float launchSpeed = drone.moveSpeed * kDroneChargeSpeedScale;
            const float windupLeft  = (drone.stateTimer > 0.0f) ? drone.stateTimer : 0.0f;

            // 足元(playerPos)ではなく体の中心を狙う（足元は地面なので身長ぶん下に刺さる）。
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

            // 発射順は先頭（localIndex が小さい機）ほど早い。

            // まだ攻撃に入っていない間だけ割り振る。
            // 撃つ合図が出た後に書き直すと、順番がリセットされ続けて永久に撃てなくなる。
            if (drone.currentState != DroneState::FireCharge)
            {
                drone.fireDelay =
                    (static_cast<float>(drone.localIndex) / static_cast<float>(total)) * kFireWindow;
            }
        }

        static constexpr float kFireWindow = 8.0f; // 全機を撃ち切る秒数。伸ばすほど発射レートが下がる
        static constexpr float kGatherTime = 0.6f; // 発射の何秒前から中心へ寄り始めるか
        static constexpr float kGatherDrop = 6.0f; // 輪の高さから何m下を発射地点にするか

        // 先読みの強さ。0=今の位置を撃つ / 1=完全な迎撃。狙いは発射の瞬間に固定されるので、
        // 歩くだけでは避けられず、回避すれば避けられる値にする。
        static constexpr float kLeadFactor = 0.5f;
    };

    // 登録（DroneFormationSystem::Initialize から呼ぶ）。
    inline void RegisterAll()
    {
        auto &reg = FormationRegistry::Instance();
        reg.Register<BarrageFormation>(DroneFormationType::Barrage);
    }

} // namespace CCL::AI::Formation::Attack
