// AIコマンド(脳)を受け取って動く、ボス本体の物理状態（筋肉）。
// BTは「どの技を出すか」を決めてトリガーを引くだけで、足を止めるのか退くのか
// 跳ぶのかは BossActionSystem が currentState を見て決める。
//
// 技の段（溜め→動く→硬直）は actionTimer と閾値で切る。段ごとにステートを作ると
// 状態数が技数×3になり、BTから見た1つの技が物理側で3つに割れて対応が読めなくなる。
#pragma once
#include <SimpleMath.h>

// ボス本体が今やっている動き。BossActionSystem が currentActionId から翻訳して書く。
// 保存しないので並びを変えてもプレハブは壊れないが、名前表 BossStateNames は同じ並びで直すこと。
enum class BossActionState : uint8_t
{
    None,     // 待機。足を止めて相手をゆっくり睨む
    Move,     // プレイヤーへ歩いて近づく
    BackStep, // 後方ステップ。被弾がかさんだら間合いを切る
    Leap,     // 跳躍。溜め→放物線で飛ぶ→着地硬直。どこへ降りるかは BossLeapLanding
};

// 跳躍の着地点の決め方。跳躍系の技は「どこへ降りるか」だけが違い、跳び方は共通。
// 技（ActionID）ごとに1つ決まり、BossActionSystem::OnEnterState が書く。
enum class BossLeapLanding : uint8_t
{
    AwayToCenter, // 中央を挟んで相手と反対側。端へ追い込まれたときの仕切り直し
    TowardTarget, // 相手の手前。跳躍攻撃（着地の衝撃で当てる）
    InPlace,      // その場。滞空（浮いている間にドローンが攻撃する）
};

// 跳躍の段。アニメ（空中ループ→着地）の切り替えにも使うので、タイマーの閾値から
// その都度計算せず、書き手を BossActionSystem の1か所にして値で持つ。
enum class BossLeapPhase : uint8_t
{
    Windup,   // 踏み切り前の溜め。着地点はまだ決まっていない
    Flight,   // 空中。曲線上の位置を追いかけて飛ぶ
    Hover,    // 頂点で停止。滞空だけが通る（曲線の進みを頂点で止めている）
    Recovery, // 着地硬直
};

/// 跳躍1種ぶんの跳び方。跳躍はこの3つの値と「着地点の決め方」だけで区別する。
/// 技ごとに別名の float を並べると、跳躍を1つ足すたびに構造体・インスペクタ定義・
/// OnEnterState の3か所へ同じ形を書き写すことになる。
struct BossLeapDef
{
    float windup = 0.3f;  // 踏み切るまでの溜め [秒]。LeapStart クリップ(0.47秒)より短くする
    float flight = 1.0f;  // 踏み切り→着地 [秒]。滞空はこの半分で頂点へ着く
    float height = 10.0f; // 放物線の頂点の高さ [m]
};

/// ボス本体の動きの調整値と、動いている最中の状態。
struct BossActionComponent
{
    // --- 移動（実効速度は JoltCharacterConfigComponent::walkSpeed が上限）---

    float walkSpeed           = 4.0f;  // 歩いて近づく速さ [m/s]
    float turnSpeed           = 8.0f;  // 移動中・技の溜め中に相手へ向き直る速さ
    float idleTurnSpeed       = 4.0f;  // 待機中の旋回。turnSpeed より遅くして「ゆっくり睨む」に見せる
    float acceleration        = 8.0f;  // 歩き出しの加速 [m/s²]
    float deceleration        = 10.0f; // 止まるときの減速 [m/s²]
    float advanceStopDistance = 7.0f;  // 「詰めながら待つ」で足を止める距離 [m]。0だと押し続けて間合いを選べない

    // --- 後方ステップ（退く距離 = backStepSpeed × backStepTime ÷ 2）---

    float backStepSpeed  = 26.0f; // 踏み切りの速さ [m/s]。backStepTime かけて直線的に0まで落とす
    float backStepHeight = 1.2f;  // 弧の頂点の高さ [m]。0にすると地上を滑る後ずさりに戻る
    float backStepTime   = 0.4f;  // 退いている長さ [秒]。変えたら AnimGraph の playbackSpeed も合わせる

    // --- 跳躍3種（跳び方 BossLeapDef ＋ 着地点を決める値ひとつ、で揃えてある）---
    //
    // 着地点が決まるのは踏み切りの瞬間なので、溜めの長さがそのまま「見てから避ける」ための
    // 予告になる。技の全体時間（＝後隙）はBTノードの param が持つ。

    BossLeapDef escapeLeap{0.25f, 1.1f, 14.0f}; // 中央へ逃げる(A_BossLeapEscape)。頂点は相手（全高約9m）の頭上を越える高さ
    float       escapeLandingRatio = 0.3f;      // 着地点の中心からの距離＝ステージ半径×これ。0だと毎回ど真ん中で狩られる

    BossLeapDef attackLeap{0.35f, 0.8f, 9.0f}; // 跳躍攻撃(A_BossLeapAttack)。射程の上限は持たない
    float       attackStandOff = 3.0f;         // 相手のどれだけ手前に降りるか [m]。0だと真上に降りてカプセルが押し合う

    BossLeapDef hoverLeap{0.3f, 0.9f, 16.0f}; // その場で滞空(A_BossAirHover)。頂点はプレイヤーの攻撃が届かない高さに
    float       hoverTime = 2.5f;             // 頂点に留まる長さ [秒]。ドローンが陣形を組んで撃ち終わるまでの猶予

    // --- ランタイム状態。書き手はすべて BossActionSystem（保存しない）---
    //
    // 保存すると「たまたま保存した瞬間の技」がプレハブへ焼き付き、次に生成された個体が
    // その状態で生まれてくる。

    float                        currentMoveSpeed = 0.0f;                             // 今の移動の速さ [m/s]。アニメの Speed に載る
    DirectX::SimpleMath::Vector3 moveDirection    = DirectX::SimpleMath::Vector3::Zero; // 今動いている向き（水平・正規化済み）
    BossActionState              currentState     = BossActionState::None;             // 今やっている動き
    float                        actionTimer      = 0.0f;                              // 今の動きに入ってからの経過 [秒]。段の切り替えはこれと閾値だけで決める

    /// 技に入った瞬間に固定した進行方向。毎フレーム取り直すとホーミング攻撃になり
    /// 原理的に避けられない。「進む向き」であって「見ている向き」ではない。
    DirectX::SimpleMath::Vector3 lockedDirection = DirectX::SimpleMath::Vector3::UnitZ;

    // 以下は跳躍の実行時の値。技に入った瞬間に調整値を写し、踏み切った瞬間に着地点を
    // 確定させる。溜めの間に着地点を決めないのは、溜めを見てから動いた相手に着地点が
    // 付いてくると「見てから避ける」が成立しないため。座標は Jolt のキャラ位置の系。

    BossLeapDef                  leapDef;                                                // 今の跳躍の跳び方（技ごとの調整値を写したもの）
    BossLeapLanding              leapLanding     = BossLeapLanding::AwayToCenter;        // 今の跳躍の着地点の決め方
    BossLeapPhase                leapPhase       = BossLeapPhase::Windup;                // 今の跳躍の段。アニメの空中→着地の切り替えにも使う
    DirectX::SimpleMath::Vector3 leapTakeoffPos  = DirectX::SimpleMath::Vector3::Zero;   // 踏み切った地点
    DirectX::SimpleMath::Vector3 leapLandingPos  = DirectX::SimpleMath::Vector3::Zero;   // 着地する地点（ステージの内側へ収めた後の値）
    float                        leapHoverTime   = 0.0f;                                 // 今の跳躍が頂点に留まる長さ [秒]。0なら止まらずそのまま降りる
    bool                         leapLandAttack  = false;                                // 着地を攻撃のクリップ(LeapLandAttack)で受けるか。アニメの LeapAttack に載る
    bool                         leapTouchedDown = false;                                // 着地の最初のフレームで速度を0にし終えたか。飛行中の速度が残って滑るのを防ぐ
};
