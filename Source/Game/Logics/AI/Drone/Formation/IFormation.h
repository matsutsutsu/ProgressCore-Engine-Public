// 陣形計算の基底インターフェース。1陣形=1クラス（switch廃止）。
//
// DOD上の設計原則（就職作品の要点）
//   仮想関数（vtable）はコンポーネントに持たせない。
//   FormationSystem が「今の陣形タイプ」で registry から IFormation を1回引き、
//   Calculate の中で全ドローンをまとめて処理する。
//     → 仮想呼び出しは「陣形の種類ごとに1回」だけ
//     → 重いループ（数百機）は連続メモリアクセスでキャッシュが効く
//   これは BT の IBTAction を registry.GetAction(id) で引くのと同じパターン。
#pragma once
#include "SimpleMath.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"

// 小隊陣形だけが使う。ポインタ渡しなので前方宣言で足りる
// （既存の陣形はこのヘッダを読むだけで、小隊の実装に引きずられない）。
struct SquadRegistry;

namespace CCL::AI::Formation
{
// 陣形計算に必要な「場の情報」。System が組み立てて渡す。
struct FormationContext
{
    DirectX::SimpleMath::Vector3 bossPos   = {};   // ボスの位置（陣形の中心）
    DirectX::SimpleMath::Vector3 playerPos = {};   // プレイヤーの位置（狙う系で使う）
    float                        time      = 0.0f; // 経過時間（回転アニメ等に使う）
    float                        dt        = 0.0f; // このフレームの経過時間（攻撃の進行度を進めるのに使う）

    // 「撃つと決めた瞬間」に焼いた狙いの向き（水平・正規化済み）と、それが有効か。
    //
    // なぜ必要か:
    //   攻撃陣形は刃や腕の向きを playerPos - bossPos から毎フレーム引き直していた。
    //   そのため技が発生した後も形ごとプレイヤーを追いかけ、横へ回り込んでも
    //   刃が一緒に回ってきて当たる。このファイルの冒頭で「ホーミングさせない＝
    //   ジャスト回避で抜けられる」と宣言している内容と、実装が食い違っていた。
    //
    //   ボス本体の突進斬りは同じ問題を lockedDirection（溜め終了時の向きで確定）で
    //   既に解いている。陣形もそれに合わせる。
    //
    // 誰が焼くか:
    //   DroneFormationSystem。「今撃て」信号の立ち上がりで1回だけ書き、信号が
    //   下りたら解除する。予備動作の間は追い、コミットしたら追わない。
    DirectX::SimpleMath::Vector3 aimDir    = DirectX::SimpleMath::Vector3::UnitZ;
    bool                         aimLocked = false;

    // 全小隊の状態（小隊陣形のみ使用。未登録なら nullptr）。
    //
    // 既存の陣形はこのフィールドを見ないので、追加しても影響を受けない。
    const ::SquadRegistry       *squads    = nullptr;

    // 狙う向き（水平・正規化済み）。
    //
    // 攻撃陣形は playerPos を直接見ず、必ずこれを使う。
    // ロック前は生の方向、ロック後は焼いた方向を返すので、呼び出し側は
    // 「今どちらの時間帯か」を意識しなくてよい。
    DirectX::SimpleMath::Vector3 AimDirection() const
    {
        using namespace DirectX::SimpleMath;
        Vector3 dir = aimLocked ? aimDir : (playerPos - bossPos);
        dir.y = 0.0f;
        if (dir.LengthSquared() < 0.001f) dir = Vector3::UnitZ;
        dir.Normalize();
        return dir;
    }
};

// 攻撃1回ぶんの寿命を誰が終わらせるか。
//
// Broadcast : ボスが「今撃て」信号を下ろしている間ずっと攻撃し続ける。
//             陣形自体が動く技（縦割り・挟み込み）は「飛び切る」概念が無いので必ずこちら。
// Individual: 各機が飛距離／経過時間で自分から切り上げ、離脱してから隊列へ戻る。
//             撃ち出したら独立して飛ぶ技（カミカゼ・小隊突撃）はこちら。
//
// 既定は Broadcast なので、既存の陣形クラスは1行も変更せずに従来動作のままになる。
enum class ChargeLifecycle : uint8_t
{
    Broadcast,
    Individual,
};

// 技1つぶんの「当たり方」。形とは別に持つ。
//
// なぜ陣形が持つのか:
//   当たり半径とダメージは DroneHitSystem の定数で、全陣形共通だった。
//   そのため「大技は痛いが避けやすい／牽制は軽いが避けにくい」という、ボス戦の
//   強弱を作る一番基本的な対比が原理的に作れなかった。
//   「その技が何であるか」は陣形が持ち、「いつ使うか」はBTが持つ、と分ける。
//
// 既定値は従来のハードコード値そのまま。上書きしない陣形は今までどおり動く。
struct FormationCombat
{
    float hitRadius       = 2.5f;  // ドローンとプレイヤーの接触半径 [m]
    float damage          = 8.0f;  // 1ヒットの基礎ダメージ
    float knockback       = 12.0f; // ノックバック強さ（0で無効）
    float hitStopDuration = 0.05f; // 命中時のヒットストップ [秒]
    float hitStopScale    = 0.0f;  // ヒットストップ中のタイムスケール（0=完全停止）
};

// 陣形の基底。派生が「1機分の目標座標」を計算する。
//
// 設計方針：
//   Calculate は「ドローン1機」を受け取り、その targetPosition を書く。
//   ループは System 側が回す（同じ陣形のドローンを連続処理）。
//   → localIndex / totalDrones を使えば「全体の何番目か」で座標が決まる。
struct IFormation
{
    virtual ~IFormation() = default;

    /**
     * 1機分の目標座標・視線を計算して drone に書き込む。
     * @param drone  対象ドローン（targetPosition 等を書き換える）
     * @param ctx    場の情報（ボス位置・プレイヤー位置・時間）
     */
    virtual void Calculate(DroneComponent &drone,
        const FormationContext            &ctx) = 0;

    // この陣形の攻撃を誰が終わらせるか。
    //
    // DroneCommandSystem が陣形ごとに1回だけ引く（機体ごとには引かない）。
    virtual ChargeLifecycle GetLifecycle() const { return ChargeLifecycle::Broadcast; }

    // 当たり方と痛さ。DroneHitSystem が引く。
    //   既定は従来のハードコード値なので、上書きしない陣形は今までどおり動く。
    virtual FormationCombat GetCombat() const { return {}; }

    // 発生前の予告を「震え」で出すか。
    //
    // true  : 形が組み上がってから撃つまでの間、その場で細かく震える。
    //         縦割り・挟み込みのように「形は完成していて、あとは振り下ろすだけ」の技は、
    //         止まって見えるので震えが唯一の予告になる。
    // false : 震えない。理由は2種類ある。
    //           - 隊形（円陣・頭上退避）＝そもそも技ではない
    //           - 予告を動きで出す技（弾幕）＝震えと動きが二重の予告になって濁る
    //
    // なぜ既定が true か:
    //   付け忘れの壊れ方を「目に見える側」へ倒すため。
    //   震え不要な陣形で付け忘れると「余計に震える」＝すぐ気づく。
    //   逆に既定を false にすると、新しい技に付け忘れたとき「予告だけ静かに
    //   消える」＝形は組み上がるので気づけない。後者のほうが遥かに危ない。
    virtual bool UsesShiverTelegraph() const { return true; }
};

} // namespace CCL::AI::Formation