// AIコマンド(脳)を受け取って動く、ボス本体の物理状態（筋肉）。
// BTは「どの技を出すか」を決めてトリガーを引くだけで、足を止めるのか踏み込むのか
// 跳ぶのかは BossActionSystem が currentState を見て決める。
//
// 技の段（溜め→動く→硬直）は actionTimer と閾値で切る。段ごとにステートを作ると
// 状態数が技数×3になり、BTから見た1つの技が物理側で3つに割れて対応が読めなくなる。
#pragma once
#include <SimpleMath.h>

// currentState は保存しないので、並びを変えてもプレハブは壊れない。
// ただしインスペクタの名前表 BossStateNames は同じ並びで直すこと。
enum class BossActionState : uint8_t
{
    None,     // 待機
    Move,     // プレイヤーへの移動中
    Melee,    // 薙ぎ払い。足を止めて斬る
    BackStep, // 後方ステップ。被弾がかさんだら間合いを切る
    Leap,     // 跳躍。溜め→放物線で飛ぶ→着地硬直。どこへ降りるかは BossLeapLanding
};

// 跳躍の着地点の決め方。跳躍系の技は「どこへ降りるか」だけが違い、跳び方は共通。
// 技（ActionID）ごとに1つ決まり、BossActionSystem::OnEnterState が書く。
enum class BossLeapLanding : uint8_t
{
    AwayToCenter, // 中央を挟んで相手と反対側。端へ追い込まれたときの仕切り直し
    TowardTarget, // 相手の手前。跳躍攻撃
};

// 跳躍の段。アニメ（空中ループ→着地）の切り替えにも使うので、タイマーの閾値から
// その都度計算せず、書き手を BossActionSystem の1か所にして値で持つ。
enum class BossLeapPhase : uint8_t
{
    Windup,   // 踏み切り前の溜め。着地点はまだ決まっていない
    Flight,   // 空中
    Strike,   // 空中のまま、着地攻撃の振り下ろしが始まった区間。動きは Flight と同じ
    Recovery, // 着地硬直
};

struct BossActionComponent
{
    // 移動。実効速度は JoltCharacterConfigComponent::walkSpeed を上限にする。
    float walkSpeed     = 4.0f;  // 様子見・通常接近
    float runSpeed      = 8.0f;  // 距離を詰める走行（将来の行動用・未使用）
    float turnSpeed     = 8.0f;  // 移動中の旋回
    float idleTurnSpeed = 4.0f;  // 待機中の旋回（ゆっくり睨む演出）
    float acceleration  = 8.0f;
    float deceleration  = 10.0f;

    // 「詰めながら待つ」を止める距離 [m]。限度が無いとプレイヤーに体当たりして
    // 押し続け、間合いを自分で選べなくなる。
    float advanceStopDistance = 7.0f;

    // 薙ぎ払いで相手を追って向きを合わせる長さ [秒]。過ぎたら技が終わるまで固定。
    // 最後まで回り続けると横へ回り込んでも刃が付いてきて「後隙を狩る」が成立しない。
    float meleeTurnTime = 0.35f;

    // 後方ステップの初速 [m/s]。backStepTime かけて直線的に 0 まで落とす
    // （等速のまま急停止すると「滑って突然止まる」に見える）。退く距離は速度×時間÷2。
    float backStepSpeed = 26.0f;

    // 弧の頂点の高さ [m]。0 にすると地上を滑る後ずさりに戻る。クリップが後ろ歩きなので
    // 地上を速く滑らせると足と合わないが、空中なら同じクリップでも「強く引いた」に見える。
    float backStepHeight = 1.2f;

    // 後退の長さ [秒]。技の全体時間はBTノードの param が持つ。ここを動かしたら
    // AnimGraph の playbackSpeed も合わせること。
    float backStepTime = 0.4f;

    // 中央へ逃げる跳躍。溜め＋滞空が着地の時刻になる。
    //   溜めは LeapStart クリップ（0.42秒）より短くしておくこと。長いと踏み切る前に
    //   クリップが終わり、地上で空中ループの絵が流れる。
    float escapeWindupTime = 0.25f; // 溜め [秒]
    float escapeFlightTime = 1.1f;  // 踏み切り→着地 [秒]

    // 頂点の高さ [m]。追い込まれた状況では相手が中央側に立っているので、その頭上を
    // 越えて逃げる軌道になる。プレイヤーのカプセルは全高約9mあるので、低いと空中で
    // ぶつかって着地の時刻がずれる。
    float escapeHeight = 14.0f;

    // 着地点の中心からの距離 = ステージ半径 × これ。0 だと毎回ど真ん中に降りるので、
    // 相手に先回りされて着地を狩られる。
    float escapeLandingRatio = 0.3f;

    // 跳躍攻撃。着地点は踏み切りの瞬間に決まるので、溜めが「見てから避ける」ための予告になる。
    float attackWindupTime = 0.35f; // 溜め [秒]。LeapStart クリップ（0.42秒）より短く
    float attackFlightTime = 0.8f;  // 踏み切り→着地 [秒]。着地点から退くための猶予
    float attackHeight     = 9.0f;  // 頂点の高さ [m]
    float attackMaxRange   = 30.0f; // 跳べる最大水平距離 [m]。無いとどこからでも届く

    // 相手のどれだけ手前に降りるか [m]。0 だと相手の上に降りてカプセル同士が押し合い、
    // 着地の位置も時刻もずれる。剣が届く距離の内側にしておくこと。
    float attackStandOff = 3.0f;

    // 着地の何秒前に振り下ろしのアニメへ切り替えるか。LeapSlam の攻撃判定の開始時刻を
    // playbackSpeed で割った値に合わせると、ちょうど着地の瞬間に刃が入る。
    // クリップを差し替えたらここも直すこと。
    float attackStrikeLead = 0.45f;

    // --- ランタイム状態。書き手はすべて BossActionSystem ---
    float                        currentMoveSpeed = 0.0f;
    DirectX::SimpleMath::Vector3 moveDirection    = DirectX::SimpleMath::Vector3::Zero;

    BossActionState currentState = BossActionState::None;
    float           actionTimer  = 0.0f; // 技に入ってからの経過 [秒]。段の切り替えはこれと閾値だけ

    // 技に入った瞬間に固定した進行方向。毎フレーム取り直すとホーミング攻撃になり
    // 原理的に避けられない。「進む向き」であって「見ている向き」ではない。
    DirectX::SimpleMath::Vector3 lockedDirection = DirectX::SimpleMath::Vector3::UnitZ;

    // 跳躍の実行時の値。踏み切った瞬間（溜めの終わり）に確定し、飛行中は変えない。
    //   溜めの間に決めないのは、溜めを見てから動いた相手に着地点が付いてくると
    //   「見てから避ける」が成立しないため。座標は Jolt のキャラ位置の系。
    BossLeapLanding              leapLanding     = BossLeapLanding::AwayToCenter;
    BossLeapPhase                leapPhase       = BossLeapPhase::Windup;
    DirectX::SimpleMath::Vector3 leapTakeoffPos  = DirectX::SimpleMath::Vector3::Zero;
    DirectX::SimpleMath::Vector3 leapLandingPos  = DirectX::SimpleMath::Vector3::Zero;
    float                        leapWindupTime  = 0.0f;
    float                        leapFlightTime  = 1.0f;
    float                        leapHeight      = 0.0f;
    float                        leapStrikeLead  = 0.0f; // 0 なら着地攻撃なし（Strike 段を通らない）
    bool                         leapTouchedDown = false; // 着地で水平の慣性を切ったか
};
