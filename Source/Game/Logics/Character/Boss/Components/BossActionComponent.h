// AIコマンド(脳)を受け取って動く、ボス本体の物理状態（筋肉）。
//
// 技が4つに増えた（薙ぎ払い・突進斬り・跳躍斬り・後方ステップ）。
//   BTは「どの技を出すか」だけを決め、アニメのトリガーを引く。
//   その技が足を止めるのか踏み込むのか跳ぶのかは、このコンポーネントの
//   currentState を見て BossActionSystem が決める。
//
// 技を1つの状態機械にまとめず「前半／後半」を時間で切る理由:
//   突進も跳躍も「溜め → 動く → 硬直」という同じ形をしていて、違うのは
//   長さと動き方だけ。段ごとにステートを増やすと状態数が技数×3になり、
//   BTから見た1つの技が物理側で3つに割れて対応が読めなくなる。
//   actionTimer 1本と閾値2つで表せる範囲は、時間で切るほうが素直。
#pragma once
#include <SimpleMath.h>

// ===================================================================================
// [ 列挙型 ] ボスの物理ステート
// ===================================================================================
//
// 並び順がそのままインスペクタの列挙値になるので、途中に挿すと
// 保存済みプレハブの値がずれる。増やすときは末尾に足すこと。
enum class BossActionState : uint8_t
{
    None,       // 待機
    Move,       // プレイヤーへの移動中
    Melee,      // 薙ぎ払い。足を止めて斬る
    DashSlash,  // 突進斬り。溜め→固定方向へ踏み込み→硬直
    JumpAttack, // 跳躍斬り。溜め→放物線で跳ぶ→着地硬直
    BackStep,   // 後方ステップ。被弾がかさんだら間合いを切る
};

// ===================================================================================
// [ 構造体 ] ボスアクション状態データ (POD)
// ===================================================================================
struct BossActionComponent
{
    // --- 移動の調整パラメータ ---
    // 行動ごとの目標速度。最終的な実効速度は JoltCharacterConfigComponent::walkSpeed を上限にする。
    float walkSpeed     = 4.0f;  // 様子見・通常接近の歩行速度
    float runSpeed      = 8.0f;  // 距離を詰める走行速度（将来の行動用）
    float chargeSpeed   = 25.0f; // 突進斬りの踏み込み速度
    float turnSpeed     = 8.0f;  // 移動中の旋回速度
    float idleTurnSpeed = 4.0f;  // 待機中の旋回速度（ゆっくり睨む演出）
    float acceleration  = 8.0f;  // 加速の速さ
    float deceleration  = 10.0f; // 減速の速さ

    // --- 突進斬りの調整パラメータ ---
    // 溜めの長さ [秒]。この間だけプレイヤーを追って向きを合わせる。
    //
    // 0にすると「見てから避ける」が成立しない。企画の近接技は
    // 予備動作 0.3〜0.5秒（読む時間はないが反応する時間はある）が基準。
    float dashWindupTime = 0.35f;

    // 踏み込んでいる長さ [秒]。溜めの直後から数える。
    float dashDriveTime = 0.35f;

    // --- 跳躍斬りの調整パラメータ ---
    // 踏み切るまでの溜め [秒] と滞空時間 [秒]。
    //
    // 合計が「着地の時刻」になる。既定の 0.10 + 0.40 = 0.50 秒に合わせて、
    // JumpAttack.json の攻撃判定を 0.45〜0.63 に置いてある。
    // ここを動かしたらシーケンサ側の判定区間も動かすこと（片方だけ触ると
    // 「着地の瞬間に斬る」がずれ、空振りか二度斬りになる）。
    float jumpWindupTime = 0.10f;
    float jumpFlightTime = 0.40f;

    float jumpHeight = 4.5f; // 放物線の頂点の高さ [m]

    // 跳べる最大水平距離 [m]。プレイヤーが遠すぎても、ここまでしか跳ばない。
    //
    // 制限が無いと画面外から瞬間移動してきたように見え、回避不能になる。
    float jumpMaxRange = 18.0f;

    // --- 後方ステップの調整パラメータ ---
    // 踏み切った瞬間の後退速度 [m/s]。ここから backStepTime かけて 0 まで落とす。
    //
    // 等速ではなく減速させるのは、止まり方が動きの読みやすさを決めるから。
    // 等速のまま急停止すると「滑って突然止まる」に見え、どこまで退くのかが
    // プレイヤーに伝わらない。
    // 直線的に減速するので、退く距離は backStepSpeed * backStepTime / 2。
    // 既定では 26 * 0.4 / 2 = 5.2m で、薙ぎ払いの間合い(8m)の外へは出ない。
    float backStepSpeed = 26.0f;

    // 後退している長さ [秒]。これを過ぎたら硬直に入る。
    //
    // 技の全体時間はBTノードの param（既定 0.9秒）が持つ。ここより短くすると
    // 退き切る前にBTが次の行動を選び、ステップが途中で消える。
    //
    // クリップ(Jog_Bwd_Start 1.3秒)を AnimGraph で2倍速にして 0.65秒に
    // 揃えてある。ここを大きく動かすなら、あちらの playbackSpeed も併せて直す。
    // 離れていると「もう止まっているのに後ろへ跳ぶ絵が続く」になる。
    float backStepTime = 0.4f;

    // --- ランタイム状態 ---
    float                        currentMoveSpeed = 0.0f;
    DirectX::SimpleMath::Vector3 moveDirection    = DirectX::SimpleMath::Vector3::Zero;

    BossActionState currentState = BossActionState::None;

    // 今の技に入ってからの経過時間 [秒]。書き手は BossActionSystem。
    // 溜め／踏み込み／硬直の段はこの値と上の閾値だけで決まる。
    float actionTimer = 0.0f;

    // 技に入った瞬間に固定した進行方向。書き手は BossActionSystem。
    //
    // なぜ固定するのか:
    //   毎フレーム向きを取り直すと、突進が最後までプレイヤーを追いかける
    //   ホーミング攻撃になり、原理的に避けられない。踏み切った後は当たらない、
    //   を成立させるために方向は入場時に1回だけ決める。
    //
    // 常に「進む向き」であって「見ている向き」ではない。後方ステップでは
    // プレイヤーと反対を向くので、体の向きは -lockedDirection 側になる。
    DirectX::SimpleMath::Vector3 lockedDirection = DirectX::SimpleMath::Vector3::UnitZ;

    // 跳躍の着地予定地点。踏み切った瞬間に確定する。書き手は BossActionSystem。
    DirectX::SimpleMath::Vector3 jumpLandingPos = DirectX::SimpleMath::Vector3::Zero;

    // 跳躍の踏み切り地点。放物線を出すのに使う。書き手は BossActionSystem。
    DirectX::SimpleMath::Vector3 jumpTakeoffPos = DirectX::SimpleMath::Vector3::Zero;
};
