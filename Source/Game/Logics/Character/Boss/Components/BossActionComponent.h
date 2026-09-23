// AIコマンド(脳)を受け取って動く、ボス本体の物理状態（筋肉）。
// BTは「どの技を出すか」を決めてトリガーを引くだけで、足を止めるのか退くのか
// 跳ぶのかは BossActionSystem が currentState を見て決める。
//
// 技の段（溜め→動く→硬直）は actionTimer と閾値で切る。段ごとにステートを作ると
// 状態数が技数×3になり、BTから見た1つの技が物理側で3つに割れて対応が読めなくなる。
#pragma once
#include <SimpleMath.h>

/// ボス本体が今やっている動き。BossActionSystem が currentActionId から翻訳して書く。
/// 保存しないので並びを変えてもプレハブは壊れないが、名前表 BossStateNames は同じ並びで直すこと。
enum class BossActionState : uint8_t
{
    None,     ///< 待機。足を止めて相手をゆっくり睨む
    Move,     ///< プレイヤーへ歩いて近づく
    BackStep, ///< 後方ステップ。被弾がかさんだら間合いを切る
    Leap,     ///< 跳躍。溜め→放物線で飛ぶ→着地硬直。どこへ降りるかは BossLeapLanding
};

/// 跳躍の着地点の決め方。跳躍系の技は「どこへ降りるか」だけが違い、跳び方は共通。
/// 技（ActionID）ごとに1つ決まり、BossActionSystem::OnEnterState が書く。
enum class BossLeapLanding : uint8_t
{
    AwayToCenter, ///< 中央を挟んで相手と反対側。端へ追い込まれたときの仕切り直し
    TowardTarget, ///< 相手の手前。跳躍攻撃（着地の衝撃で当てる）
};

/// 跳躍の段。アニメ（空中ループ→着地）の切り替えにも使うので、タイマーの閾値から
/// その都度計算せず、書き手を BossActionSystem の1か所にして値で持つ。
enum class BossLeapPhase : uint8_t
{
    Windup,   ///< 踏み切り前の溜め。着地点はまだ決まっていない
    Flight,   ///< 空中。曲線上の位置を追いかけて飛ぶ
    Recovery, ///< 着地硬直
};

/// ボス本体の動きの調整値と、動いている最中の状態。
struct BossActionComponent
{
    // ---------------------------------------------------------------------
    // 移動（実効速度は JoltCharacterConfigComponent::walkSpeed が上限）
    // ---------------------------------------------------------------------

    /// 歩いて近づく速さ [m/s]。
    float walkSpeed = 4.0f;

    /// 走って距離を詰める速さ [m/s]。まだどの技も使っていない。
    float runSpeed = 8.0f;

    /// 移動中・技の溜め中に相手へ向き直る速さ。大きいほど速く正対する。
    float turnSpeed = 8.0f;

    /// 待機中に相手へ向き直る速さ。turnSpeed より遅くして「ゆっくり睨む」に見せる。
    float idleTurnSpeed = 4.0f;

    /// 歩き出しの加速 [m/s²]。
    float acceleration = 8.0f;

    /// 止まるときの減速 [m/s²]。
    float deceleration = 10.0f;

    /// 「詰めながら待つ」で足を止める距離 [m]。限度が無いとプレイヤーに体当たりして
    /// 押し続け、間合いを自分で選べなくなる。
    float advanceStopDistance = 7.0f;

    // ---------------------------------------------------------------------
    // 後方ステップ
    // ---------------------------------------------------------------------

    /// 踏み切りの速さ [m/s]。backStepTime かけて直線的に 0 まで落とす
    /// （等速のまま急停止すると「滑って突然止まる」に見える）。退く距離は速度×時間÷2。
    float backStepSpeed = 26.0f;

    /// 弧の頂点の高さ [m]。0 にすると地上を滑る後ずさりに戻る。クリップが後ろ歩きなので
    /// 地上を速く滑らせると足と合わないが、空中なら同じクリップでも「強く引いた」に見える。
    float backStepHeight = 1.2f;

    /// 退いている長さ [秒]。残りは硬直で、技の全体時間はBTノードの param が持つ。
    /// ここを動かしたら AnimGraph の playbackSpeed も合わせること。
    float backStepTime = 0.4f;

    // ---------------------------------------------------------------------
    // 中央へ逃げる跳躍（A_BossLeapEscape）。溜め＋滞空が着地の時刻になる
    // ---------------------------------------------------------------------

    /// 踏み切るまでの溜め [秒]。LeapStart クリップ（Jump_Start・0.47秒）より短くしておくこと。
    /// 長いと踏み切る前にクリップが終わり、地上で空中ループの絵が流れる。
    float escapeWindupTime = 0.25f;

    /// 踏み切りから着地までの長さ [秒]。
    float escapeFlightTime = 1.1f;

    /// 放物線の頂点の高さ [m]。追い込まれた状況では相手が中央側に立っているので、
    /// その頭上を越える軌道になる。プレイヤーは全高約9mあるので、低いと空中でぶつかる。
    float escapeHeight = 14.0f;

    /// 着地点の中心からの距離 = ステージ半径 × これ。0 だと毎回ど真ん中に降りるので、
    /// 相手に先回りされて着地を狩られる。
    float escapeLandingRatio = 0.3f;

    // ---------------------------------------------------------------------
    // 跳躍攻撃（A_BossLeapAttack）。相手の位置へ大きく跳び、着地の衝撃で当てる。
    // 相手がどれだけ離れていても届かせるので射程は持たない（ステージの外へは出ない）。
    // 攻撃判定は着地のクリップ（LeapLandAttack）が持つ。
    // ---------------------------------------------------------------------

    /// 踏み切るまでの溜め [秒]。着地点は踏み切りの瞬間に決まるので、これが
    /// 「見てから避ける」ための予告になる。LeapStart クリップ（Jump_Start・0.47秒）より短く。
    float attackWindupTime = 0.35f;

    /// 踏み切りから着地までの長さ [秒]。着地点から退くための猶予。
    float attackFlightTime = 0.8f;

    /// 放物線の頂点の高さ [m]。
    float attackHeight = 9.0f;

    /// 相手のどれだけ手前に降りるか [m]。0 だと相手の上に降りてカプセル同士が押し合い、
    /// 着地の位置も時刻もずれる。着地の攻撃判定が届く距離の内側にしておくこと。
    float attackStandOff = 3.0f;

    // ---------------------------------------------------------------------
    // ランタイム状態。書き手はすべて BossActionSystem（保存しない）
    // ---------------------------------------------------------------------

    /// 今の移動の速さ [m/s]。アニメの Speed パラメータに載る（BossAnimSyncSystem）。
    float currentMoveSpeed = 0.0f;

    /// 今動いている向き（水平・正規化済み）。
    DirectX::SimpleMath::Vector3 moveDirection = DirectX::SimpleMath::Vector3::Zero;

    /// 今やっている動き。
    BossActionState currentState = BossActionState::None;

    /// 今の動きに入ってからの経過 [秒]。技の段の切り替えはこれと閾値だけで決める。
    float actionTimer = 0.0f;

    /// 技に入った瞬間に固定した進行方向。毎フレーム取り直すとホーミング攻撃になり
    /// 原理的に避けられない。「進む向き」であって「見ている向き」ではない。
    DirectX::SimpleMath::Vector3 lockedDirection = DirectX::SimpleMath::Vector3::UnitZ;

    // 以下は跳躍の実行時の値。技に入った瞬間に調整値を写し、踏み切った瞬間に着地点を
    // 確定させる。溜めの間に着地点を決めないのは、溜めを見てから動いた相手に着地点が
    // 付いてくると「見てから避ける」が成立しないため。座標は Jolt のキャラ位置の系。

    /// 今の跳躍の着地点の決め方。
    BossLeapLanding leapLanding = BossLeapLanding::AwayToCenter;

    /// 今の跳躍の段。アニメの空中→着地の切り替えにも使う。
    BossLeapPhase leapPhase = BossLeapPhase::Windup;

    /// 踏み切った地点。
    DirectX::SimpleMath::Vector3 leapTakeoffPos = DirectX::SimpleMath::Vector3::Zero;

    /// 着地する地点。ステージの内側に収めた後の値。
    DirectX::SimpleMath::Vector3 leapLandingPos = DirectX::SimpleMath::Vector3::Zero;

    /// 今の跳躍の溜め [秒]（技ごとの調整値を写したもの）。
    float leapWindupTime = 0.0f;

    /// 今の跳躍の踏み切り→着地 [秒]（同上）。
    float leapFlightTime = 1.0f;

    /// 今の跳躍の頂点の高さ [m]（同上）。
    float leapHeight = 0.0f;

    /// 着地を攻撃のクリップ（LeapLandAttack）で受けるか。アニメの LeapAttack に載る。
    bool leapLandAttack = false;

    /// 着地した最初のフレームで速度を 0 にし終えたか。飛行中の速度が残って滑るのを防ぐ。
    bool leapTouchedDown = false;
};
