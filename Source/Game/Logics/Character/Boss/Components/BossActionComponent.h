#pragma once
// ===========================================================================
// 概要: ボス本体の動き（移動・後方ステップ・跳躍・突進）の設定値と実行時の状態を持つ Component。
//
// 責務:
//   - 歩き・向き直り・加減速の設定値を持つ
//   - 後方ステップ、跳躍3種、突進攻撃の設定値を持つ
//   - 今の動き(currentState)と経過時間、跳躍・突進の段と着地点などの実行時の状態を持つ
//
// 備考:
//   BT はどの技を出すかを決めてトリガーを引くだけで、止まるのか退くのか跳ぶのかは
//   BossActionSystem が currentState を見て決める。実行時の状態を書くのは BossActionSystem だけ。
//
//   技の段（溜め→動く→硬直）は actionTimer としきい値で切り替える。段ごとにステートを作ると
//   状態数が技数×3になり、BT から見た1つの技が3つに分かれて対応が読みにくくなる。
// ===========================================================================
#include <SimpleMath.h>

// ボス本体が今やっている動き。BossActionSystem が currentActionId から変換して書く。
// 保存しないので並びを変えてもプレハブは壊れないが、名前表 BossStateNames も同じ並びで直すこと。
enum class BossActionState : uint8_t
{
    None,     // 待機。足を止め、相手の方へゆっくり向き直る
    Move,     // プレイヤーへ歩いて近づく
    Strafe,   // 横歩き。相手を見たまま、間合いを保って回り込む（様子見）
    BackStep, // 後方ステップ。被弾がかさんだら間合いを切る
    Leap,     // 跳躍。溜め→放物線で飛ぶ→着地硬直。どこへ降りるかは BossLeapLanding
    Dash,     // 突進攻撃。後ろへ浮きながら下がる→止まって狙う→下りながら直進→硬直
};

// 跳躍の着地点の決め方。跳躍系の技は「どこへ降りるか」だけが違い、跳び方は共通。
// 技（ActionID）ごとに1つ決まり、BossActionSystem::OnEnterState が書く。
enum class BossLeapLanding : uint8_t
{
    AwayToCenter, // 中央を挟んで相手と反対側。端へ追い込まれたときの仕切り直し
    TowardTarget, // 相手の手前。跳躍攻撃（着地の衝撃で当てる）
    InPlace,      // その場。滞空（浮いている間にドローンが攻撃する）
};

// 跳躍の段。アニメ（空中ループ→着地）の切り替えにも使うので、タイマーのしきい値から
// その都度計算せず、BossActionSystem だけが書く値として持つ。
enum class BossLeapPhase : uint8_t
{
    Windup,   // 踏み切り前の溜め。着地点はまだ決まっていない
    Flight,   // 空中。曲線上の位置を追いかけて飛ぶ
    Hover,    // 頂点で停止。滞空だけが通る（曲線の進みを頂点で止めている）
    Recovery, // 着地硬直
};

// 突進攻撃の段。跳躍と同じく、アニメの切り替えに使うので値で持つ。
enum class BossDashPhase : uint8_t
{
    Retreat,  // 後ろへ浮きながら下がる
    Aim,      // 空中で止まり、相手を向き続ける（予告）
    Windup,   // 空中で止まったまま突進の構えに入る。入った瞬間に方向を固定し、以後は向きを変えない
    Charge,   // 固定した方向へ、空中から地面へ下りながら突っ込む
    Travel,   // 地面に着いた後も同じ方向へ走り続ける。間合いを大きく離す
    Stop,     // 走りを緩めて止まる
    Recovery, // 止まった後の硬直
};

// 跳躍1種類分の跳び方。跳躍はこの3つの値と「着地点の決め方」だけで区別する。
// 技ごとに別名の float を並べると、跳躍を1つ足すたびに構造体・インスペクタ定義・
// OnEnterState の3か所へ同じ形を書き写すことになる。
struct BossLeapDef
{
    float windup = 0.4f;  // 踏み切るまでの溜め [秒]。LeapStart クリップ(0.47秒)より短くする
    float flight = 2.0f;  // 踏み切り→着地 [秒]。滞空はこの半分で頂点へ着く
    float height = 30.0f; // 放物線の頂点の高さ [m]
};

struct BossActionComponent
{
    // --- 移動（実効速度は JoltCharacterConfigComponent::walkSpeed が上限）---

    float walkSpeed           = 4.0f;  // 歩いて近づく速さ [m/s]
    float turnSpeed           = 8.0f;  // 移動中・技の溜め中に相手へ向き直る速さ
    float idleTurnSpeed       = 4.0f;  // 待機中の向き直りの速さ。turnSpeed より遅くし、ゆっくり相手を見るように見せる
    float acceleration        = 8.0f;  // 歩き出しの加速 [m/s²]
    float deceleration        = 10.0f; // 止まるときの減速 [m/s²]
    float advanceStopDistance = 7.0f;  // 「詰めながら待つ」で足を止める距離 [m]。0だと押し続けて間合いを選べない

    // --- 横歩き（様子見）---
    //
    // 進む向きは「相手への方向の直角」を毎フレーム取り直す。結果として相手を中心に
    // 円を描くので、間合いは放っておいても保たれる。ワールド座標で向きを固定すると、
    // 相対角度が変わった先で横歩きに見えなくなる。

    float strafeSpeed        = 3.0f;  // 横へ回る速さ [m/s]。walkSpeed より遅くする（速いと逃げに見える）
    float strafeTurnSpeed    = 6.0f;  // 回りながら相手へ向き直る速さ。turnSpeed と idleTurnSpeed の間
    float strafeKeepDistance = 12.0f; // 保ちたい間合い [m]。間合い補正を有効にしたときだけ効く
    float strafeDistanceGain = 0.5f;  // 間合いのずれをどれだけ詰めるか。0 で純粋な円

    // --- 後方ステップ（退く距離 = backStepSpeed × backStepTime ÷ 2）---

    float backStepSpeed  = 26.0f; // 踏み切りの速さ [m/s]。backStepTime かけて直線的に0まで落とす
    float backStepHeight = 1.2f;  // 弧の頂点の高さ [m]。0にすると地上を滑る後ずさりに戻る
    float backStepTime   = 0.4f;  // 退いている長さ [秒]。変えたら AnimGraph の playbackSpeed も合わせる

    // --- 跳躍3種（跳び方 BossLeapDef ＋ 着地点を決める値ひとつ、で揃えてある）---
    //
    // 着地点が決まるのは踏み切りの瞬間なので、溜めの長さがそのまま「見てから避ける」ための
    // 予告になる。技の全体時間（＝後隙）はBTノードの param が持つ。

    BossLeapDef escapeLeap{0.25f, 1.1f, 40.0f}; // 中央へ逃げる(A_BossLeapEscape)。頂点は相手（全高約9m）の頭上を越える高さ
    float       escapeLandingRatio = 0.3f;      // 着地点の中心からの距離＝ステージ半径×これ。0だと毎回ど真ん中で狩られる

    BossLeapDef attackLeap{0.35f, 2.0f, 30.0f}; // 跳躍攻撃(A_BossLeapAttack)。射程の上限は持たない
    float       attackStandOff = 3.0f;         // 相手のどれだけ手前に降りるか [m]。0だと真上に降りてカプセルが押し合う

    BossLeapDef hoverLeap{0.3f, 0.9f, 16.0f}; // その場で滞空(A_BossAirHover)。頂点はプレイヤーの攻撃が届かない高さに
    float       hoverLandRecovery = 0.63f;    // 着地硬直 [秒]。頂点に留まる長さ＝BTノードの全体時間−溜め−上昇下降−これ

    // --- 突進攻撃(A_BossDashAttack)。硬直＝BTノードの全体時間の残り ---
    //
    // 狙うのは「方向」だけ。溜めに入った瞬間に相手への向きを固定し、その方向へ突っ込んだ後も
    // 走り続ける。相手の位置で止まらないので、近距離の攻撃と間合いを離す移動を兼ねる。
    //
    // 避ける機会は溜めの間に置く。突っ込む速さは見てから反応できる速さではないので、
    // 「構えた＝方向が決まった」と読ませ、溜めの間に横へ動けば避けられるようにしてある。

    float dashRetreatTime     = 0.5f;  // 後ろへ下がる長さ [秒]
    float dashRetreatDistance = 6.0f;  // 後ろへ下がる距離 [m]
    float dashRetreatHeight   = 3.0f;  // 下がりながら浮く高さ [m]。突進はここから地面へ下りる
    float dashAimTime         = 0.5f;  // 空中で止まって狙う長さ [秒]。相手を向き続ける
    float dashWindupTime      = 0.4f;  // 突進の構え（溜め）の長さ [秒]。方向は固定済みで、この間に避ける
    float dashSpeed           = 45.0f; // 突っ込む速さ [m/s]
    float dashDistance        = 15.0f; // 空中から地面へ突っ込む距離 [m]。この後の前進は別に数える
    float dashTravelSpeed     = 25.0f; // 着地後に走り続ける速さ [m/s]
    float dashTravelTime      = 1.0f;  // 走り続ける長さ [秒]。進む距離 ≒ 速さ × これ
    float dashStopTime        = 0.4f;  // 走りを緩めて止まるまで [秒]

    // --- 実行時の状態。書くのはすべて BossActionSystem（保存しない）---
    //
    // 保存すると、保存した瞬間の技の状態がプレハブに残り、次に生成された個体が
    // その状態から始まる。

    float                        currentMoveSpeed = 0.0f;                             // 今の移動の速さ [m/s]。アニメの Speed に載る
    DirectX::SimpleMath::Vector3 moveDirection    = DirectX::SimpleMath::Vector3::Zero; // 今動いている向き（水平・正規化済み）
    BossActionState              currentState     = BossActionState::None;             // 今やっている動き
    float                        actionTimer      = 0.0f;                              // 今の動きに入ってからの経過 [秒]。段の切り替えはこれと閾値だけで決める

    float strafeSign    = 1.0f;   // 横歩きの向き。+1=自分から見て右回り / -1=左回り。入るたびに反転する
    bool  strafeFlipped = false;  // 壁に当たって向きを反転済みか。1回だけにしないと境界上でばたつく

    // 技に入った瞬間に固定した進行方向。毎フレーム取り直すと相手を追い続ける攻撃になり、
    // 避けられなくなる。「進む向き」であって「見ている向き」ではない。
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

    // 以下は突進の実行時の値。下がり始めの地点は最初のフレームに、突進の終点は予告の
    // 終わりに1回だけ決める。

    BossDashPhase                dashPhase       = BossDashPhase::Retreat;               // 今の突進の段。アニメの切り替えにも使う
    bool                         dashStarted     = false;                                // 下がり始めの地点を記録し終えたか
    DirectX::SimpleMath::Vector3 dashStartPos    = DirectX::SimpleMath::Vector3::Zero;   // 下がり始めた地点。高さはここを地面とみなす
    DirectX::SimpleMath::Vector3 dashAimPos      = DirectX::SimpleMath::Vector3::Zero;   // 下がり切って狙う地点（空中）
    DirectX::SimpleMath::Vector3 dashEndPos      = DirectX::SimpleMath::Vector3::Zero;   // 突進の終点（地面・ステージの内側へ収めた後の値）
    float                        dashChargeTime  = 0.0f;                                 // 突進にかける時間 [秒]。距離÷速さ
};
