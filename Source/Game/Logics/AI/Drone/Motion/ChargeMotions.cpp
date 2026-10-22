#include "ChargeMotions.h"

#include <algorithm>
#include <iterator>

#include "Game/Logics/AI/Drone/Motion/DroneMotionCommon.h"
#include "Game/Logics/AI/Drone/Squad/SquadState.h" // バネ追従の強さ（SquadTuning::followGain）
#include "Engine/ECS/Core/CCL_World.h"

using namespace DirectX::SimpleMath;

namespace CCL::AI::Motion
{
    namespace
    {
        // ============================================================
        // 直進（Straight）── 弾幕
        // ============================================================
        //
        //   - 発射の順番待ち（fireDelay）の間は持ち場を追う
        //   - タメ: 一度止まってから撃つ方向へ加速する
        //   - リリース: 撃つ方向へ高速で直進する。当たり判定が開くのはこの間だけ
        //
        // 撃つ方向（fireDirection）は陣形が書き、書かなかったときだけ BeginStraightCharge が決める。
        // ここでは向きを変えない（ホーミングしない）。

        // 直進の突撃の調整値。タメ → リリース の2段。
        struct StraightChargeTuning
        {
            float windup;          // 突撃前のタメ全体の長さ [秒]
            float lungeTime;       // タメの後半で加速にかける長さ [秒]
            float lungeSpeed;      // 加速の終速 [m/s]
            float windupTurnRate;  // タメ中に機首を向け直す速さ [1/s]
            float speedScale;      // リリースの速さ（moveSpeed 倍）
            float releaseTurnRate; // リリース中に機首を向け直す速さ [1/s]
        };

        // タメは「静止 → 加速」の2段でジャスト回避を読ませる。
        //
        //     |<----------------   windup (0.4秒)   ---------------->|
        //     |<-- 静止 (0.2秒) -->|<--   lungeTime (0.2秒)   -->| 射出
        //
        //   静止の長さは、タメ全体から加速の長さを引いた残りで決まる。
        //
        //   中央で一度止まることで「集まった」と「撃った」が別々の出来事として読める。
        //   定速にすると速度の段差が2回でき、動きが途切れて見える。
        //
        // リリースの速さの実体が DroneTuning.h にあるのは、弾幕の陣形も同じ値で
        // 「飛翔時間 = 距離 ÷ 射出速度」を出すため。
        //
        // 上げすぎると擦り抜ける。1フレームの移動距離が当たり半径の2倍を超えると、
        // 跨いで通り過ぎる（半径2.5m・60fps なら 300 m/s が上限の目安）。
        constexpr StraightChargeTuning kStraightCharge{
            .windup          = 0.4f,
            .lungeTime       = 0.2f,
            .lungeSpeed      = 20.0f,
            .windupTurnRate  = 20.0f,
            .speedScale      = kDroneChargeSpeedScale,
            .releaseTurnRate = 25.0f,
        };

        class StraightChargeMotion : public IChargeMotion
        {
        public:
            void Move(TransformComponent &trans, DroneComponent &drone, float dt,
                      const MotionContext &ctx) const override
            {
                // 処理フロー:
                // 1. 発射待ちの間は持ち場を追う
                // 2. タメ: 一度止まってから撃つ方向へ加速する
                // 3. リリース: 撃つ方向へ高速で直進する

                // --- 発射待ち ---

                // fireDelay > 0 の間は自分の順番を待つ。

                // 待っている間も陣形の指示に従わせる。
                // ここで静止させると Strike の瞬間に輪の回転まで止まり、撃つ機以外が静止する。
                // 陣形が「輪を回しつつ番が近い機だけ中心へ寄せる」と書けるのは、ここが動いているから。
                if (drone.fireDelay > 0.0f)
                {
                    drone.fireDelay -= dt;
                    MoveToSlot(trans, drone, dt, ctx);
                    return;
                }

                // 突撃方向が無ければ飛ばさない（方向は陣形か BeginStraightCharge が書く）。
                if (drone.fireDirection.LengthSquared() <= 0.001f)
                {
                    drone.currentVelocity = Vector3::Zero;
                    return;
                }

                if (drone.chargeWindup > 0.0f)
                {
                    // --- タメ ---

                    // 中央で一度止まってから撃つ方向へ加速する。

                    // ここで震えなどの予告は足さない。
                    // 予告は陣形側（中心へ集まる動き）が済ませているので、足すと予告が二重になる。

                    // この区間は当たり判定を持たない（IsDangerous）。
                    drone.chargeWindup -= dt;

                    // 残り時間から 0 → lungeSpeed へ加速する。残りが lungeTime より
                    // 多い間は 0 のまま＝その場で静止する（専用のフラグもタイマーも要らない）。
                    float lunge = 0.0f;
                    if (drone.chargeWindup < kStraightCharge.lungeTime)
                        lunge = kStraightCharge.lungeSpeed * (1.0f - (std::max)(0.0f, drone.chargeWindup) / kStraightCharge.lungeTime);

                    drone.currentVelocity = drone.fireDirection * lunge;

                    TurnToward(trans, drone.targetLookDirection, /*includePitch*/ false, kStraightCharge.windupTurnRate, dt);
                }
                else
                {
                    // --- リリース ---

                    // 高速直進。この間だけ「危険」＝当たり判定が開く。
                    drone.currentVelocity = drone.fireDirection * (drone.moveSpeed * kStraightCharge.speedScale);

                    TurnToward(trans, drone.targetLookDirection, /*includePitch*/ false, kStraightCharge.releaseTurnRate, dt);
                }
            }

            // 発射待ち（fireDelay > 0）とタメ（chargeWindup > 0）の間は当たらない。
            // 当たるのは、リリースで直進しているときだけ。
            bool IsDangerous(const DroneComponent &drone) const override
            {
                return drone.fireDelay <= 0.0f && drone.chargeWindup <= 0.0f;
            }
        };

        // ============================================================
        // 剛体追従（RigidFollow）── データ陣形（縦割り・挟み込み・横なぎ等）
        // ============================================================
        //
        //   - 形が動いている間は、陣形が動かす targetPosition に毎フレーム一致させる（壁がたわまない）
        //   - 形が動き切ったら Idle のバネへ渡し、惰性で収める
        //
        // タメ（chargeWindup）は使わない。突撃中はずっと当たる（IsDangerous は既定のまま）。

        // 剛体追従の調整値。
        struct RigidFollowTuning
        {
            float inertia;         // 形が動いている間の速度を currentVelocity へ残す割合
            float inertiaMaxScale; // 残す速度の上限（moveSpeed 倍）
            float turnRate;        // 機首を向け直す速さ [1/s]
        };

        // 終わり際に惰性を残す。形が動き切ったあとは Idle のバネへ渡す。
        //
        // inertia が 0 だと、刃や腕が閉じ切った瞬間に全機が空中で同時に静止し、不自然に見える。
        //
        // 倍率をいくつにしても剛体そのものは壊れない。
        // 残した速度ぶんを座標から引いてから積分へ渡すので、着地点は targetPosition ちょうどになる。
        //
        // 上限を置くのは、腕の外側の機は回転半径ぶん速度が跳ね上がり、そのまま残すと収まらずに飛んでいくため。
        constexpr RigidFollowTuning kRigidFollow{
            .inertia         = 0.35f,
            .inertiaMaxScale = 0.6f,
            .turnRate        = 25.0f, // MoveToTarget と同じ速さで向け直す（DroneMotionCommon.cpp）
        };

        class RigidFollowMotion : public IChargeMotion
        {
        public:
            void Move(TransformComponent &trans, DroneComponent &drone, float dt,
                      const MotionContext &ctx) const override
            {
                // 追従モードは陣形自体が動く攻撃（縦割りのヒンジ回転など）。
                // 直進せず、Formation が毎フレーム動かす targetPosition を追う。

                // --- 動き切った後 ---

                // 形が動き切ったら剛体をやめ、Idle のバネへ渡して惰性で収める。
                // 閉じ切った座標に固定し続けると、全機が同時に静止して不自然に見えるため。

                // 手を離すのは動きだけで、当たり方は変わらない。
                // 危険な区間かどうかは currentState が持っている（DroneHitSystem）。
                if (drone.attackProgress >= 1.0f)
                {
                    HoldSlot(trans, drone, dt, ctx);
                    return;
                }

                // --- 剛体 ---

                // 動いている間は targetPosition に一致させる＝剛体（壁がたわまない）。

                // ただし速度はゼロにせず、実際に出ている速さの一部を残しておく。
                // ここを 0 にすると、上の受け渡しで Idle が静止から動き出すことになり、結局は同じ止まり方に戻る。
                Vector3 carried = Vector3::Zero;
                if (dt > 0.0f)
                {
                    carried = (drone.targetPosition - Vector3(trans.position)) / dt * kRigidFollow.inertia;

                    const float maxSpeed = drone.moveSpeed * kRigidFollow.inertiaMaxScale;
                    if (carried.LengthSquared() > maxSpeed * maxSpeed)
                    {
                        carried.Normalize();
                        carried *= maxSpeed;
                    }
                }
                drone.currentVelocity = carried;

                // 共通の積分がこの後 currentVelocity * dt を足すので、その分だけ手前に置く。
                // 足し戻った結果が targetPosition ちょうどになるので、上の倍率や上限をどういじっても形は歪まない。
                trans.position = drone.targetPosition - carried * dt;

                if (drone.targetLookDirection.LengthSquared() > 0.001f)
                {
                    Vector3 lookDir = drone.targetLookDirection;
                    lookDir.Normalize();
                    TurnToward(trans, lookDir, /*includePitch*/ true, kRigidFollow.turnRate, dt);
                }
            }
        };

        // ============================================================
        // バネ追従（SoftFollow）── 小隊突撃
        // ============================================================
        //
        //   - 隊列を組んだまま、持ち場へバネで寄る。剛体追従と違って形が歪む
        //   - 機首を、目標ではなく実際の進行方向へ向ける
        //
        // 歪むことがこの技の見た目の要点。目標へバネで寄るので、速いほど後ろへ流れ、曲がると遅れて曲がる。
        // 剛体追従にすると隊列がまったく歪まず、「1枚の板が近づいてくる」絵になる。
        //
        // タメ（chargeWindup）は使わない。突撃中はずっと当たる（IsDangerous は既定のまま）。
        //
        // バネの強さは小隊の調整値（SquadTuning::followGain）を BeginFrame で写し取る。
        // 後ろの席ほど弱める倍率は、小隊の陣形が drone.springScale に書く。

        // バネ追従の調整値。バネの強さは SquadTuning から実行中に写し取る（BeginFrame）。
        struct SoftFollowTuning
        {
            float damping;       // 今の速度を目標の速度へ寄せる速さ [1/s]
            float maxSpeedScale; // 出せる速さの上限（moveSpeed 倍）
            float turnRate;      // 機首を向け直す速さ [1/s]
        };

        // 上限は、小隊のアンカーの突入速度を上回る必要がある（低いと隊列から離れて置いていかれる）。
        constexpr SoftFollowTuning kSoftFollow{.damping = 10.0f, .maxSpeedScale = 2.4f, .turnRate = 12.0f};

        class SoftFollowMotion : public IChargeMotion
        {
        public:
            void BeginFrame(CCL::ECS::World &world) override
            {
                // 小隊の調整 UI で変えたバネの強さを写し取る。無ければ前の値（既定値）のまま。
                if (world.HasResource<SquadRegistry>())
                    m_followGain = world.GetResource<SquadRegistry>().tuning.followGain;
            }

            void Move(TransformComponent &trans, DroneComponent &drone, float dt,
                      const MotionContext &ctx) const override
            {
                const float pGain = m_followGain * drone.springScale; // 後方の機ほど弱める＝しなる（springScale は小隊の陣形が書く）

                // 先渡しの速度 ＋ 目標からのずれの補正（バネ）。先渡しがあるぶんバネは小さくてよい。
                //
                // Idle / MoveToTarget（DroneMotionCommon の ApplySpring）と違い、上限は先渡しの速度ごと掛ける。
                Vector3 displacement   = drone.targetPosition - trans.position;
                Vector3 targetVelocity = drone.followFeedForward + displacement * pGain;

                // アンカーの突入速度を追い越せる上限にする（低いと隊列から離れて置いていかれる）。
                const float maxSpeed = drone.moveSpeed * kSoftFollow.maxSpeedScale;
                if (targetVelocity.LengthSquared() > maxSpeed * maxSpeed)
                {
                    targetVelocity.Normalize();
                    targetVelocity *= maxSpeed;
                }

                drone.currentVelocity = Vector3::Lerp(drone.currentVelocity, targetVelocity,
                                                      (std::min)(kSoftFollow.damping * dt, 1.0f));

                // 機首は目標ではなく実際の進行方向へ向ける。隊列のしなりが姿勢にも出る。
                Vector3 look = drone.currentVelocity;
                if (look.LengthSquared() > 0.01f)
                {
                    look.Normalize();
                    TurnToward(trans, look, /*includePitch*/ true, kSoftFollow.turnRate, dt);
                }
            }

        private:
            float m_followGain = 10.0f; // バネの強さ。SquadTuning が無いときはこの既定値
        };

        // ============================================================
        // 表
        // ============================================================

        // 動き方は1種類につき1つを全機で共有する。
        // const にしないのは BeginFrame で調整値を写し取るため（Move の間は読むだけ）。
        StraightChargeMotion gStraight{};
        RigidFollowMotion    gRigidFollow{};
        SoftFollowMotion     gSoftFollow{};

        // 並びは ChargeMode の宣言順（添字がそのまま値になる）。
        IChargeMotion *const kTable[] = {&gStraight, &gRigidFollow, &gSoftFollow};
    } // namespace

    const IChargeMotion &GetChargeMotion(ChargeMode mode)
    {
        const size_t index = static_cast<size_t>(mode);
        return (index < std::size(kTable)) ? *kTable[index] : gStraight;
    }

    void BeginChargeMotionsFrame(CCL::ECS::World &world)
    {
        for (IChargeMotion *motion : kTable)
            motion->BeginFrame(world);
    }

    void BeginStraightCharge(DroneComponent &drone, const ChargeBeginInfo &info)
    {
        // 処理フロー:
        // 1. 移動方式を直進に戻す
        // 2. タメの時間を入れる
        // 3. 突撃方向が無ければ決める

        // 直進モードから始める。陣形自体が動く攻撃は Formation 側が毎フレーム上書きするので、
        // 前回の攻撃の追従モードが直進系の攻撃へ漏れない。
        drone.chargeMode = ChargeMode::Straight;

        // タメ時間。追従の動き方（剛体追従・バネ追従）はこれを使わない。
        drone.chargeWindup = kStraightCharge.windup;

        // 陣形が予備動作中に方向を設定済みなら、それを使う。
        if (drone.fireDirection.LengthSquared() >= 0.001f) return;

        // 陣形が方向を書かなかったときは、持ち場からプレイヤーへの向きにする。
        // プレイヤーが居なければモデルの前方（-Z）へ飛ばす。
        Vector3 dir = -Vector3::UnitZ;
        if (info.hasPlayer)
        {
            Vector3 toPlayer = info.playerPos - drone.targetPosition;
            if (toPlayer.LengthSquared() > 0.001f)
            {
                toPlayer.Normalize();
                dir = toPlayer;
            }
        }

        drone.fireDirection       = dir;
        drone.targetLookDirection = dir;
    }

} // namespace CCL::AI::Motion
