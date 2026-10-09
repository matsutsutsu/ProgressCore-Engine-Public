#include "StraightChargeMotion.h"

#include <algorithm>

#include "Game/Logics/AI/Drone/Motion/DroneMotionCommon.h"

using namespace DirectX::SimpleMath;

namespace CCL::AI::Motion
{
    namespace
    {
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
    } // namespace

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
        drone.stateTimer = kStraightCharge.windup;

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

    void StraightChargeMotion::Move(TransformComponent &trans, DroneComponent &drone, float dt,
                                    const MotionContext &ctx) const
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

        if (drone.stateTimer > 0.0f)
        {
            // --- タメ ---

            // 中央で一度止まってから撃つ方向へ加速する。

            // ここで震えなどの予告は足さない。
            // 予告は陣形側（中心へ集まる動き）が済ませているので、足すと予告が二重になる。

            // この区間は当たり判定を持たない（IsDangerous）。
            drone.stateTimer -= dt;

            // 残り時間から 0 → lungeSpeed へ加速する。残りが lungeTime より
            // 多い間は 0 のまま＝その場で静止する（専用のフラグもタイマーも要らない）。
            float lunge = 0.0f;
            if (drone.stateTimer < kStraightCharge.lungeTime)
                lunge = kStraightCharge.lungeSpeed * (1.0f - (std::max)(0.0f, drone.stateTimer) / kStraightCharge.lungeTime);

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

} // namespace CCL::AI::Motion
