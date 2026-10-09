#pragma once
// ===========================================================================
// 概要: 陣形計算の基底インターフェース（IFormation）と、陣形へ渡す情報の型。
//
// 責務:
//   - 1機分の持ち場を計算する Calculate を定める
//   - 攻撃の寿命（ChargeLifecycle）・当たり方（FormationCombat）・予告の有無を陣形ごとに返す
//   - 突撃の始め方（OnChargeBegin）と切り上げ方（IsChargeOver）を陣形ごとに決める
//   - 陣形計算に要る情報（FormationContext）をまとめる
//
// 備考:
//   1陣形=1クラスにして switch を使わない。
//
//   仮想関数はコンポーネントに持たせない。DroneComponent は数値だけの配列のまま保ち、
//   陣形は FormationRegistry から種類で引く。数百機のループは連続アクセスのまま回る。
//
//   呼び出しの回数は System ごとに違う。DroneCommandSystem と DroneHitSystem は種類ごとに1回だけ引く。
//   DroneFormationSystem は機体ごとに呼ぶが、全機がほぼ同じ陣形なので分岐予測が当たる。
// ===========================================================================
#include "SimpleMath.h"
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"
#include "Game/Logics/AI/Drone/Motion/StraightChargeMotion.h" // 突撃の始め方の既定

namespace CCL::ECS { class World; }

namespace CCL::AI::Formation
{
    // 陣形計算に必要な「場の情報」。System が組み立てて渡す。
    struct FormationContext
    {
        DirectX::SimpleMath::Vector3 bossPos      = {}; // ボスの位置（陣形の中心）
        DirectX::SimpleMath::Vector3 bossVelocity = {}; // 陣形の中心が動いている速度 [m/s]。bossPos の前フレームとの差から出す
        DirectX::SimpleMath::Vector3 playerPos    = {}; // プレイヤーの位置（足元。間合いの計算に使う）

        // 狙うのはこちら。playerPos は足元なので、そこを狙うと身長ぶん下を撃つ。
        DirectX::SimpleMath::Vector3 playerCenter = {};

        // プレイヤーの速度 [m/s]。飛翔時間のある技だけが偏差射撃に使う
        // （置き技は「撃つ瞬間の向きへコミットする」のが仕様なので読まない）。
        DirectX::SimpleMath::Vector3 playerVelocity = {};
        float                        time           = 0.0f; // 経過時間 [秒]（陣形の回転などに使う）
        float                        dt             = 0.0f; // このフレームの経過時間 [秒]（攻撃の進行度に使う）

        // 「撃つと決めた瞬間」に固定した狙いの向き（水平・正規化済み）と、それが有効か。
        //
        // 毎フレーム取り直すと、技の発生後も形ごとプレイヤーを追い、横へ回り込んでも刃が一緒に回ってきて当たる。
        //
        // 固定するのは DroneFormationSystem。信号の立ち上がりに1回だけ書き、信号が下りたら解除する。
        DirectX::SimpleMath::Vector3 aimDir    = DirectX::SimpleMath::Vector3::UnitZ;
        bool                         aimLocked = false;

        /**
         * @brief 狙う向き（水平・正規化済み）を返す。攻撃陣形は playerPos を直接見ず必ずこれを使う。
         * @return 固定前はボス→プレイヤーの方向、固定後は固定した方向。
         *         向きが求まらない（ほぼ同じ位置）ときは +Z。
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

    // 攻撃1回ぶんの寿命を誰が終わらせるか。既定は Broadcast。
    enum class ChargeLifecycle : uint8_t
    {
        Broadcast,  // 信号が立っている間ずっと攻撃。陣形自体が動く技（縦割り等）は必ずこちら
        Individual, // 各機が飛距離／経過時間で自分から切り上げる。弾幕のみ
                    //   （小隊突撃は隊列を保ったまま動き切る必要があるので Broadcast）
    };

    // 技1つぶんの「当たり方」。形とは別に、陣形ごとに変えられるように持つ。
    // 全陣形で共通の定数にすると「大技は痛いが避けやすい／牽制は軽いが避けにくい」
    // という対比が作れない。
    struct FormationCombat
    {
        // 現在このフィールドは誰も読んでいない。
        // 実際の接触半径はプレハブの Jolt Sphere Collider が持つ（Drone.json の radius）。
        //
        // 技ごとに変えたいなら、DroneHitSystem から JoltSphereColliderComponent へ書く配線が要る。
        float hitRadius       = 2.5f;  // ドローンとプレイヤーの接触半径 [m]
        float damage          = 8.0f;  // 1ヒットの基礎ダメージ
        float knockback       = 12.0f; // ノックバック強さ（0で無効）
        float hitStopDuration = 0.05f; // 命中時のヒットストップ [秒]
        float hitStopScale    = 0.0f;  // ヒットストップ中のタイムスケール（0=完全停止）

        // 同じ機体が同じ相手にもう一度当たるまでの間隔 [秒]。負なら突撃1回につき1ヒット。
        // 既定を負にするのは、縦割りのように形ごと通過する技が、重なっている間ずっと当たり続けないため。
        float rehitInterval = -1.0f;
    };

    // 陣形の基底。派生が localIndex / totalDrones から「1機分の目標座標」を計算する。
    struct IFormation
    {
        virtual ~IFormation() = default;

        /**
         * @brief フレームの最初に1回、全機の Calculate より前に呼ぶ。DroneFormationSystem が呼ぶ。
         *
         * 陣形が自分だけで使うデータ（小隊の状態など）を、World から自分で取りに行く口。
         * 全陣形に共通の情報は FormationContext で渡すので、ほとんどの陣形は何もしない。
         *
         * @param world ドローンが居る World。
         */
        virtual void BeginFrame(CCL::ECS::World &world) {}

        // 1機分の目標座標・視線を計算して drone に書き込む。
        virtual void Calculate(DroneComponent         &drone,
                               const FormationContext &ctx) = 0;

        // 攻撃を誰が終わらせるか。DroneCommandSystem がフレームに1回だけ引く。
        virtual ChargeLifecycle GetLifecycle() const { return ChargeLifecycle::Broadcast; }

        /**
         * @brief 突撃（FireCharge）へ入った瞬間に1回だけ行う準備。DroneBrainSystem が呼ぶ。
         *
         * タメの長さや撃つ方向の決め方は技ごとに違うので、技の側に持たせる。
         *
         * 既定は直進の突撃の始め方（Motion::BeginStraightCharge）。
         * 陣形追従の技（縦割り・小隊など）も既定のままでよい。同じフレームの Calculate が
         * 移動方式と向きを書き直し、追従の動き方はタメを使わないため。
         *
         * 並列ループ（ForEachParallel）から呼ぶので、陣形の側の状態は書き換えない（const）。
         *
         * @param drone 突撃を始める機体。
         * @param info  プレイヤーの位置など、始めるときに要る情報。
         */
        virtual void OnChargeBegin(DroneComponent &drone, const Motion::ChargeBeginInfo &info) const
        {
            Motion::BeginStraightCharge(drone, info);
        }

        /**
         * @brief 個別寿命（Individual）の突撃を、この機が切り上げるかを返す。DroneBrainSystem が聞く。
         *
         * 終わり方の数値は技ごとに違う（届かせたい距離は発射位置と間合いで決まる）ので、技の側に持たせる。
         * Broadcast の技では呼ばれない。
         *
         * @param flownDistance 飛び出してから飛んだ距離 [m]。発射待ちとタメの間は数えない。
         * @param flownTime     飛び出してからの経過時間 [秒]。
         * @return 切り上げて離脱へ移るなら true。
         */
        virtual bool IsChargeOver(float flownDistance, float flownTime) const { return false; }

        // 当たり方と痛さ。DroneHitSystem が引く。
        virtual FormationCombat GetCombat() const { return {}; }

        // 発生前の予告を「震え」で出すか。false にするのは隊形と、動きで予告する技（弾幕）。
        // 既定を true にしているのは、false 既定だと新しい技で予告が出ないことに気づきにくいため。
        virtual bool UsesShiverTelegraph() const { return true; }

        // 持ち場がボスに付いて動くか。true なら、ボスの速度を先渡しの速度（followFeedForward）として
        // 乗せ、バネには持ち場からのずれだけを直させる。
        //
        // 乗せないと、ボスが突進や跳躍で速く動いたときにバネの上限の速さでしか追えず、
        // 動き終わった後にのろのろと付いてくる。
        //
        // false にするのは、ボス以外を基準に動く陣形（小隊は先頭機を基準にする）。
        virtual bool FollowsBoss() const { return true; }

        // 跳躍・滞空で浮いている間も、ボスの実際の高さを陣形の中心にするか。
        //
        // 既定の false は「中心を地面の高さに残す」。
        // 地面を薙ぐ技が、ボスが浮いた瞬間にプレイヤーの頭上を素通りしないようにするため。
        //
        // true にするのは、ボスの周りに付いて回るだけの隊形（頭上円陣など）。
        virtual bool FollowsBossHeight() const { return false; }

        // 画面に出す技の名前。名乗るのはデータ陣形だけで、C++ 実装は nullptr を返す。
        // BTエディタが「枠24」ではなく「縦割り」と出せるのは、これを引くため。
        virtual const char *DisplayName() const { return nullptr; }
    };

} // namespace CCL::AI::Formation
