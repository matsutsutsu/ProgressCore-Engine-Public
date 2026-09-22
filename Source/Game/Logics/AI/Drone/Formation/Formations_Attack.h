// C++ で書く攻撃陣形。データ（Assets/Formations/*.json）で書けない技だけをここに置く。
//
// データで書けないのは、機体が自分の状態を覚えて振る舞いを変える技。弾幕は機体ごとに
// 発射の順番(fireDelay)を持ち、撃つ瞬間まで先読みで狙い直し、撃った後は各機が
// 独立して攻撃を終える。データ陣形は「全機に同じ進行度を配って形を動かす」だけなので、
// この3つはどれも表せない。
//
// 縦割り・挟み込み・横なぎ・波状縦割りは Assets/Formations/*.json にある（Formations_DataDriven が解釈する）。
#pragma once
#include "Game/Logics/AI/Drone/Formation/IFormation.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include "Game/Logics/AI/Drone/Formation/Formations_Basic.h" // 弾幕が頭上リングを借りる
#include <cmath>
#include <algorithm>

namespace CCL::AI::Formation::Attack
{
    // 弾幕（KamikazeSwarm）── 要求回避＝ジャスト回避。
    // 頭上の輪を回したまま1機ずつ中心へ降り、そこからプレイヤーへ高速で撃ち出す。
    //   輪の上で待つ → 自分の番の kGatherTime 秒前に中心へ寄る（これが予告）
    //   → fireDelay が 0 で発射（以降は直進）
    //
    // 置き技（縦割り・挟み込み）と違い、発射の瞬間まで狙い続ける「1発ずつ今のあなたを
    // 狙う」技。撃ち出した後は直進するので避ける余地はそこで担保される。
    struct KamikazeSwarmFormation : public IFormation
    {
        // 撃ち出したら各機が独立して飛ぶので、終了判断は個体に持たせる（帰投がバラバラになる）。
        ChargeLifecycle GetLifecycle() const override { return ChargeLifecycle::Individual; }

        // 中心へ集まる移動そのものが予告。足すと予告が二重になってどちらを読むか濁る。
        bool UsesShiverTelegraph() const override { return false; }

        void Calculate(DroneComponent &drone, const FormationContext &ctx) override
        {
            using namespace DirectX::SimpleMath;

            // 陣形が手を引くのは「飛び出した後」だけ。順番待ちも中央でのタメもまだ撃って
            // いないので狙いを更新し続ける（stateTimer を見落とすと着弾の 0.5 秒以上前の
            // 座標を撃つことになり、歩いているだけで当たらなくなる）。
            const bool launched = (drone.currentState == DroneState::FireCharge) &&
                                  (drone.fireDelay <= 0.0f) && (drone.stateTimer <= 0.0f);
            if (launched) return;

            drone.chargeMode = ChargeMode::Straight;

            const int total = (drone.totalDrones > 0) ? drone.totalDrones : 1;

            // 待機位置は頭上円陣と同じ輪。回転位相まで共有するので、BT が
            // 頭上円陣 → 弾幕 と切り替えてもドローンは動かない。
            const Vector3 slot = ctx.bossPos + Basic::HighRing::SlotOffset(
                                                   drone.localIndex, drone.totalDrones, ctx.time);

            // 発射地点は輪の中心の少し下。
            const Vector3 muzzle =
                ctx.bossPos + Vector3::UnitY * (Basic::HighRing::kHeight - kGatherDrop);

            // 自分の番が近づくほど中心へ寄る。二乗で寄せるのは、序盤をほぼ動かさず
            // 「直前にすっと吸い込まれる」形にするため（線形だといつ撃つのか読めない）。
            float gather = 0.0f;
            if (drone.currentState == DroneState::FireCharge && drone.fireDelay <= 0.0f)
            {
                // 中央でのタメ中は持ち場を発射地点のままにする。輪へ戻すと下の飛翔時間が
                // 「輪から撃つ」前提になり、実際の発射地点とずれた先読みになる。
                gather = 1.0f;
            }
            else if (drone.fireDelay > 0.0f && drone.fireDelay < kGatherTime)
            {
                const float t = 1.0f - (drone.fireDelay / kGatherTime);
                gather        = t * t;
            }

            drone.targetPosition = Vector3::Lerp(slot, muzzle, gather);

            // 偏差射撃。今いる場所を撃つと飛翔時間ぶん必ず後ろへ外れるので、
            // 狙う先 ＝ 今の位置 ＋ 速度 × (タメの残り ＋ 距離÷射出速度) を解く。
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

            // 発射順（先頭ほど早い）。撃つ合図が出た後に書き直すと順番がリセットされ続けて
            // 永久に撃てなくなるので、まだ攻撃に入っていない間だけ割り振る。
            if (drone.currentState != DroneState::FireCharge)
            {
                drone.fireDelay =
                    (static_cast<float>(drone.localIndex) / static_cast<float>(total)) * kFireWindow;
            }
        }

        static constexpr float kFireWindow = 8.0f; // 全機を撃ち切る秒数。伸ばすほど発射レートが下がる
        static constexpr float kGatherTime = 0.6f; // 発射の何秒前から中心へ寄り始めるか
        static constexpr float kGatherDrop = 6.0f; // 輪の高さから何m下を発射地点にするか

        // 先読みの強さ。0=今の位置を撃つ / 1=完全な迎撃。狙いは発射の瞬間に凍るので、
        // 「歩くだけで避けられる」を潰しつつ「避ければ避けられる」を残す値にする。
        static constexpr float kLeadFactor = 0.5f;
    };

    // 登録（DroneFormationSystem::Initialize から呼ぶ）。
    inline void RegisterAll()
    {
        auto &reg = FormationRegistry::Instance();
        reg.Register<KamikazeSwarmFormation>(DroneFormationType::KamikazeSwarm);
    }

} // namespace CCL::AI::Formation::Attack
