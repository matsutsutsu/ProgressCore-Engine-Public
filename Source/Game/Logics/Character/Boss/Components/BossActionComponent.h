// AIコマンド(脳)を受け取って動く、ボス本体の物理状態（筋肉）。
// BTは「どの技を出すか」を決めてトリガーを引くだけで、足を止めるのか踏み込むのか
// 跳ぶのかは BossActionSystem が currentState を見て決める。
//
// 技の段（溜め→動く→硬直）は actionTimer と閾値で切る。段ごとにステートを作ると
// 状態数が技数×3になり、BTから見た1つの技が物理側で3つに割れて対応が読めなくなる。
#pragma once
#include <SimpleMath.h>

// 並び順がそのままインスペクタの列挙値になる。増やすときは末尾に足すこと
// （途中に挿すと保存済みプレハブの値がずれる）。
enum class BossActionState : uint8_t
{
    None,       // 待機
    Move,       // プレイヤーへの移動中
    Melee,      // 薙ぎ払い。足を止めて斬る
    DashSlash,  // 突進斬り。溜め→固定方向へ踏み込み→硬直
    JumpAttack, // 跳躍斬り。溜め→放物線で跳ぶ→着地硬直
    BackStep,   // 後方ステップ。被弾がかさんだら間合いを切る
};

struct BossActionComponent
{
    // 移動。実効速度は JoltCharacterConfigComponent::walkSpeed を上限にする。
    float walkSpeed     = 4.0f;  // 様子見・通常接近
    float runSpeed      = 8.0f;  // 距離を詰める走行（将来の行動用・未使用）
    float chargeSpeed   = 25.0f; // 突進斬りの踏み込み
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

    // 突進斬り。溜めの間だけプレイヤーを追う（0 だと「見てから避ける」が成立しない）。
    float dashWindupTime = 0.35f; // 溜め [秒]
    float dashDriveTime  = 0.35f; // 踏み込み [秒]。到達距離 = chargeSpeed * この値

    // 跳躍斬り。溜め＋滞空の合計が着地の時刻になる。アニメの攻撃判定が滞空区間へ
    // 収まるようにすること（ずれると空中攻撃なのに着地後に斬る絵になる）。
    float jumpWindupTime = 0.10f;
    float jumpFlightTime = 0.40f;

    float jumpHeight   = 4.5f;  // 放物線の頂点の高さ [m]
    float jumpMaxRange = 18.0f; // 跳べる最大水平距離 [m]。無いと瞬間移動に見える

    // 後方ステップの初速 [m/s]。backStepTime かけて直線的に 0 まで落とす
    // （等速のまま急停止すると「滑って突然止まる」に見える）。退く距離は速度×時間÷2。
    float backStepSpeed = 26.0f;

    // 弧の頂点の高さ [m]。0 にすると地上を滑る後ずさりに戻る。クリップが後ろ歩きなので
    // 地上を速く滑らせると足と合わないが、空中なら同じクリップでも「強く引いた」に見える。
    float backStepHeight = 1.2f;

    // 後退の長さ [秒]。技の全体時間はBTノードの param が持つ。ここを動かしたら
    // AnimGraph の playbackSpeed も合わせること。
    float backStepTime = 0.4f;

    // --- ランタイム状態。書き手はすべて BossActionSystem ---
    float                        currentMoveSpeed = 0.0f;
    DirectX::SimpleMath::Vector3 moveDirection    = DirectX::SimpleMath::Vector3::Zero;

    BossActionState currentState = BossActionState::None;
    float           actionTimer  = 0.0f; // 技に入ってからの経過 [秒]。段の切り替えはこれと閾値だけ

    // 技に入った瞬間に固定した進行方向。毎フレーム取り直すとホーミング攻撃になり
    // 原理的に避けられない。「進む向き」であって「見ている向き」ではない。
    DirectX::SimpleMath::Vector3 lockedDirection = DirectX::SimpleMath::Vector3::UnitZ;

    // 跳躍の着地予定地点と踏み切り地点。踏み切った瞬間に確定する。
    DirectX::SimpleMath::Vector3 jumpLandingPos = DirectX::SimpleMath::Vector3::Zero;
    DirectX::SimpleMath::Vector3 jumpTakeoffPos = DirectX::SimpleMath::Vector3::Zero;
};
