#pragma once
// ===========================================================================
// 概要: 陣形計算の基底インターフェース（IFormation）と、陣形へ渡す情報の型
//
// 責務:
//   - 1機分の持ち場を計算する Calculate を定める
//   - 当たり方（FormationCombat）・技の時間割（TechniqueTiming）・予告の有無を陣形ごとに返す
//   - 突撃の始め方（OnChargeBegin）と切り上げ方（IsChargeOver）を陣形ごとに決める
//   - 陣形計算に要る情報（FormationContext）をまとめる
//
// 備考:
//   技は増えていくので、1陣形=1クラスにして switch を使わない
//   DroneComponent には仮想関数を持たせず数値だけの配列に保ち、陣形は FormationRegistry から種類で引く
// ===========================================================================
#include "SimpleMath.h"
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"
#include "Game/Logics/AI/Drone/Motion/DroneMotion.h"

namespace CCL::ECS { class World; }

namespace CCL::AI::Formation
{
    // 陣形計算に必要な場の情報。DroneFormationSystem が組み立てて渡す
    struct FormationContext
    {
        DirectX::SimpleMath::Vector3 bossPos        = {};   // ボスの位置（陣形の中心）
        DirectX::SimpleMath::Vector3 bossVelocity   = {};   // 陣形の中心が動いている速度 [m/s]
        DirectX::SimpleMath::Vector3 playerPos      = {};   // プレイヤーの足元。間合いの計算に使う
        DirectX::SimpleMath::Vector3 playerCenter   = {};   // プレイヤーの体の中心。狙うのはこちら
        DirectX::SimpleMath::Vector3 playerVelocity = {};   // プレイヤーの速度 [m/s]。偏差射撃の計算に使う
        float                        time           = 0.0f; // 経過時間 [秒]。陣形の回転などに使う
        float                        dt             = 0.0f; // このフレームの経過時間 [秒]

        DirectX::SimpleMath::Vector3 aimDir    = DirectX::SimpleMath::Vector3::UnitZ; // 攻撃時の固定した狙いの向き（水平・正規化済み）
        bool                         aimLocked = false;                               // aimDir が有効か

        /**
         * @brief 狙う向き（水平・正規化済み）を返す。攻撃陣形は playerPos を直接見ず必ずこれを使う
         *
         * 撃った後も狙いを取り直すと、横へ回り込んでも形ごと追ってきて当たるため
         *
         * @return 固定前はボス→プレイヤーの方向、固定後は固定した方向
         *         向きが求まらない（ほぼ同じ位置）ときは +Z
         */
        DirectX::SimpleMath::Vector3 AimDirection() const
        {
            using namespace DirectX::SimpleMath;

            Vector3 dir = aimLocked ? aimDir : (playerPos - bossPos);
            dir.y       = 0.0f;
            if (dir.LengthSquared() < 0.001f) dir = Vector3::UnitZ;
            dir.Normalize();
            return dir;
        }
    };

    // 技1つぶんの当たり方。大技は痛く牽制は軽い、という差を技ごとに付ける
    // 接触半径はここに持たない（プレハブ Drone.json の Jolt Sphere Collider が決める）
    struct FormationCombat
    {
        float damage          = 8.0f;  // 1ヒットの基礎ダメージ
        float knockback       = 12.0f; // ノックバック強さ（0で無効）
        float hitStopDuration = 0.05f; // 命中時のヒットストップ [秒]
        float hitStopScale    = 0.0f;  // ヒットストップ中のタイムスケール（0=完全停止）
        float rehitInterval   = -1.0f; // 同じ相手に再び当たるまでの間隔 [秒]。負なら突撃1回につき1ヒット
    };

    // 技1つぶんの時間割。BT の「ドローンの技」がこの順に進める
    struct TechniqueTiming
    {
        float telegraph = 1.0f; // 陣形を指示してから撃つまで [秒]
        float recovery  = 1.0f; // 撃ってから頭上円陣へ戻すまで [秒]
        float rest      = 0.0f; // 戻してから技を終えるまで [秒]
    };

    // 陣形の基底。派生が localIndex / totalDrones から「1機分の目標座標」を計算する
    class IFormation
    {
    public:
        virtual ~IFormation() = default;

        /**
         * @brief 陣形が自分だけで使うデータ（小隊の状態など）を World から取りに行く
         * @param world ドローンがいる World
         * @note フレームの最初に1回、全機の Calculate より前に呼ばれる
         */
        virtual void BeginFrame(CCL::ECS::World &world) {}

        // 1機分の目標座標・視線を計算して drone に書き込む
        virtual void Calculate(DroneComponent         &drone,
                               const FormationContext &ctx) = 0;

        /**
         * @brief 突撃（FireCharge）へ入った瞬間の準備（タメ・撃つ方向）
         *
         * 既定は直進の突撃の始め方。追従の技も既定のままでよい（同じフレームの Calculate が移動方式を書き直す）
         *
         * @param drone 突撃を始める機体
         * @param info  プレイヤーの位置など
         * @note 並列ループから呼ばれるので、陣形の側の状態は書き換えない
         */
        virtual void OnChargeBegin(DroneComponent &drone, const Motion::ChargeBeginInfo &info) const
        {
            Motion::BeginStraightCharge(drone, info);
        }

        /**
         * @brief 突撃中のこの機が、自分で突撃を切り上げるかを返す
         *
         * 既定は false で、合図が下りるまで全機が攻撃し続ける（縦割り・小隊など）
         * 撃った機から順に帰す技（弾幕）だけが、飛距離や時間で true を返す
         *
         * @param flownDistance 飛び出してから飛んだ距離 [m]。発射待ちとタメの間は数えない
         * @param flownTime     飛び出してからの経過時間 [秒]
         * @return 切り上げて離脱へ移るなら true
         */
        virtual bool IsChargeOver(float flownDistance, float flownTime) const { return false; }

        /**
         * @brief この陣形から別の陣形へ切り替わる機の後始末
         *
         * 技が自分だけで使う値（弾幕の fireDelay など）は、技が自分で片付ける
         *
         * @param drone 切り替わる機体
         */
        virtual void OnExit(DroneComponent &drone) const {}

        // 当たり方と痛さ
        virtual FormationCombat GetCombat() const { return {}; }

        // 技の時間割を out に書いて true を返す。技でない隊形（円陣）は false
        virtual bool TryGetTiming(TechniqueTiming &out) const { return false; }

        // 発生前の予告を震えで出すか。動きで予告できない技（縦割りなど）のため
        // 既定を true にするのは、false 既定だと新しい技で予告が出ないことに気づきにくいため
        virtual bool UsesShiverTelegraph() const { return true; }

        // 持ち場がボスに付いて動くか。true ならボスの速度を先渡しし、ボスが速く動いても置いていかれない
        // false にするのは、ボス以外を基準に動く陣形（小隊）
        virtual bool FollowsBoss() const { return true; }

        // ボスが跳躍で浮いている間も、実際の高さを中心にするか
        // 既定の false は中心を地面の高さに残す。true にするのはボスに付いて回るだけの隊形（頭上円陣など）
        virtual bool FollowsBossHeight() const { return false; }

        // 画面に出す技の名前。名乗るのはデータ陣形だけで、C++ 実装は nullptr
        virtual const char *DisplayName() const { return nullptr; }
    };

} // namespace CCL::AI::Formation
