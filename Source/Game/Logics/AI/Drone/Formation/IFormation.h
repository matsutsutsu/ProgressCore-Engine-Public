// 陣形計算の基底インターフェース。1陣形=1クラス（switch廃止）。
// 仮想関数をコンポーネントに持たせず、System が registry から1回引いてループを回す
// ＝仮想呼び出しは陣形の種類ごとに1回だけで、数百機のループは連続アクセスのまま。
#pragma once
#include "SimpleMath.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"

// 小隊陣形だけが使う。前方宣言で足りるので、既存の陣形は小隊の実装に引きずられない。
struct SquadRegistry;

namespace CCL::AI::Formation
{
    // 陣形計算に必要な「場の情報」。System が組み立てて渡す。
    struct FormationContext
    {
        DirectX::SimpleMath::Vector3 bossPos   = {}; // ボスの位置（陣形の中心）
        DirectX::SimpleMath::Vector3 playerPos = {}; // プレイヤーの位置（足元。間合いの計算に使う）

        // 狙うのはこちら。playerPos は足元なので、そこを狙うと身長ぶん下を撃つ。
        DirectX::SimpleMath::Vector3 playerCenter = {};

        // プレイヤーの速度 [m/s]。飛翔時間のある技だけが偏差射撃に使う
        // （置き技は「撃つ瞬間の向きへコミットする」のが仕様なので読まない）。
        DirectX::SimpleMath::Vector3 playerVelocity = {};
        float                        time           = 0.0f; // 経過時間（回転アニメ等に使う）
        float                        dt             = 0.0f; // このフレームの経過時間（攻撃の進行度に使う）

        // 「撃つと決めた瞬間」に焼いた狙いの向き（水平・正規化済み）と、それが有効か。
        //   毎フレーム引き直すと技の発生後も形ごとプレイヤーを追い、横へ回り込んでも
        //   刃が一緒に回ってきて当たる。焼くのは DroneFormationSystem で、信号の
        //   立ち上がりに1回だけ書いて下りたら解除する。
        DirectX::SimpleMath::Vector3 aimDir    = DirectX::SimpleMath::Vector3::UnitZ;
        bool                         aimLocked = false;

        // 全小隊の状態（小隊陣形のみ使用。未登録なら nullptr）。
        const ::SquadRegistry *squads = nullptr;

        /// 狙う向き（水平・正規化済み）。攻撃陣形は playerPos を直接見ず必ずこれを使う。
        /// ロック前は生の方向、ロック後は焼いた方向を返す。
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

    // 技1つぶんの「当たり方」。形とは別に持つ。全陣形共通の定数だった頃は
    // 「大技は痛いが避けやすい／牽制は軽いが避けにくい」という対比が作れなかった。
    struct FormationCombat
    {
        // ※ 現在このフィールドは誰も読んでいない。実際の接触半径はプレハブの
        //    Jolt Sphere Collider が持つ（Drone.json の radius）。技ごとに変えたいなら
        //    DroneHitSystem から JoltSphereColliderComponent へ書く配線が要る。
        float hitRadius       = 2.5f;  // ドローンとプレイヤーの接触半径 [m]
        float damage          = 8.0f;  // 1ヒットの基礎ダメージ
        float knockback       = 12.0f; // ノックバック強さ（0で無効）
        float hitStopDuration = 0.05f; // 命中時のヒットストップ [秒]
        float hitStopScale    = 0.0f;  // ヒットストップ中のタイムスケール（0=完全停止）
    };

    // 陣形の基底。派生が localIndex / totalDrones から「1機分の目標座標」を計算する。
    struct IFormation
    {
        virtual ~IFormation() = default;

        /// 1機分の目標座標・視線を計算して drone に書き込む。
        virtual void Calculate(DroneComponent         &drone,
                               const FormationContext &ctx) = 0;

        /// 攻撃を誰が終わらせるか。陣形ごとに1回だけ引かれる。
        virtual ChargeLifecycle GetLifecycle() const { return ChargeLifecycle::Broadcast; }

        /// 当たり方と痛さ。DroneHitSystem が引く。
        virtual FormationCombat GetCombat() const { return {}; }

        /// 発生前の予告を「震え」で出すか。false にするのは隊形と、動きで予告する技（弾幕）。
        /// 既定が true なのは、false 既定だと新しい技で予告だけ静かに消えるため。
        virtual bool UsesShiverTelegraph() const { return true; }
    };

} // namespace CCL::AI::Formation
