///**
// * @file BossActionComponent.h
// * @brief AIコマンド(脳)を受け取って動く、ボス本体の物理・アニメーション状態（筋肉）
// */
//#pragma once
//#include <cstdint>
//#include <SimpleMath.h>
//
// // アクションのタイミング定数を一括管理
//namespace BossTimings {
//    // --- 近接攻撃 ---
//    constexpr float Melee_Duration = 1.0f;
//
//
//    constexpr float Move_Duration = 0.5f;
//
//    // --- チャージ攻撃 (タメと予兆を強調するため合計 4.0秒 に延長) ---
//    constexpr float Charge_Total = 5.5f;      // ★ 浮遊タメ(2.5s) + 突進(1.0s) + ブレーキ(0.5s)
//    // ★追加: 突進予備動作(TackleStart)のアニメーションを開始する時間
//    // (突進が始まる 1.5s のさらに 1.0秒前 に設定し、じっくり飛び上がりを見せる)
//    constexpr float Charge_AnimStart = 2.5f;
//    constexpr float Charge_DashStart = 1.5f;  // 空中突進を開始する時間
//    constexpr float Charge_BrakeTime = 0.5f;  // ブレーキを開始する時間
//
//    // --- ジャンプ攻撃 (合計 3.0秒 に延長) ---
//    constexpr float JumpAttack_Total = 3.0f; // 地上タメ(0.5s) + 飛翔(1.5s) + 着地硬直(1.0s)
//    constexpr float JumpAttack_Liftoff = 2.5f; // 実際に足が地面から離れる時間
//    constexpr float JumpAttack_Land = 1.0f; // 地面に着地する時間
//
//    // --- 回避行動 (合計 1.2秒 に延長して細分化) ---
//    constexpr float Evade_Total = 1.8f; // 全体時間
//    constexpr float Evade_Airborne = 1.6f; // 0.2秒の Start(タメ) の後、飛び退き開始
//    constexpr float Evade_Land = 0.1f; // 残り0.2秒で End(着地) アニメーションへ
//
//    // --- アニメーショントリガー ---
//    constexpr float Melee_AnimTrigger = Melee_Duration;
//    constexpr float Charge_AnimTrigger = Charge_Total;       // 開始と同時に浮遊＆チャージアニメ
//    constexpr float JumpAttack_StartTrigger = JumpAttack_Total;   // 開始と同時にしゃがみ(タメ)アニメ
//
//    // 怯みモーションの定数
//    constexpr float Flinch_Duration = 0.4f; // 怯みで硬直する時間（アニメーションに合わせて調整）
//
//    // フェーズ移行のタイムライン (合計5.0秒)
//    constexpr float PhaseTransition_Total = 5.0f;
//
//    // ジャンプ＆滞空 (Jump to Hover)
//    constexpr float JumpToHover_Total = 2.0f;    // 地上タメ(0.5s) + 飛翔(1.5s)
//    constexpr float JumpToHover_Liftoff = 1.5f;  // 0.5s経過で離陸
//
//    // 降下＆着地 (Hover to Land)
//    constexpr float HoverToLand_Total = 1.0f;    // 降下(0.5s) + 着地硬直(0.5s)
//    constexpr float HoverToLand_Land = 0.5f;     // 0.5s経過で着地
//
//    // 横移動の基本時間
//    constexpr float Strafe_Duration = 2.0f;
//    constexpr float Strafe_LoopStart = 1.8f;  // 最初の0.2秒はStart(踏み込み)モーション
//    constexpr float Strafe_EndTrigger = 0.2f; // 残り0.2秒でEnd(ブレーキ)モーションへ
//
//    // ★追加: 死亡演出のタイムライン
//    constexpr float Death_TotalDuration = 3.0f; // 演出の全体時間
//    constexpr float Death_FlinchTime = 2.5f;    // 残り2.5秒の時点で倒れ込み開始（最初の0.5秒は怯み）
//
//
//    constexpr float EscapeToCenter_Total = 2.0f;    
//}
//
//// ===================================================================================
//// [ 列挙型 ] ボスの物理ステート
//// ===================================================================================
//enum class BossActionState : uint8_t {
//    None,               // 待機
//    Move,               // プレイヤーへの移動中
//    Melee,              // 近接攻撃中
//    Charge,             // 突進中
//    JumpAttack,         // ジャンプ攻撃中
//    Evade,              // 回避行動中
//    Flinch,             // 被弾して怯んでいる状態
//    PhaseTransition,    // フェーズ転換
//    JumpToHover,        // 上空の指定位置へジャンプして滞空
//    HoverToLand,        // 滞空状態からプレイヤーへ急降下着地
//    Strafe,             // 横移動
//    Dead,               // 死亡ステート
//	EscapeJump,          // 中央へ逃げるためのジャンプ
//};
//
//
//// ===================================================================================
//// [ 構造体 ] ボスアクション状態データ (POD)
//// ===================================================================================
//struct BossActionComponent {
//    // --- 12バイト型 (Vector3) ---
//    DirectX::SimpleMath::Vector3 moveDirection = DirectX::SimpleMath::Vector3::Zero;
//    DirectX::SimpleMath::Vector3 actionDirection = DirectX::SimpleMath::Vector3::Zero;
//
//    // --- 4バイト型 (float: 性能パラメータ) ---
//    float walkSpeed = 3.0f;
//    float strafeSpeed = 4.0f; // 横に回るスピード（少し速めが良い）
//    float chargeSpeed = 25.0f;
//    float turnSpeed = 10.0f;
//    float evadeSpeed = 25.0f;
//    float acceleration = 10.0f;
//    float deceleration = 15.0f;
//    float jumpAttackApexHeight = 8.0f;
//    float chargeHoverVelocity = 6.0f;
//
//    // =======================================================
//    // 形態によって変化するチャージ攻撃の可変タイマー
//    // =======================================================
//    float currentChargeTotal = 3.5f;     // 第1形態: 3.5s (第2形態は 5.5s)
//    float currentChargeAnimStart = 2.5f; // 第1形態: 2.5s (第2形態は 4.5s)
//
//    //  滞空制御用の変数
//    bool isHovering = false;       // 現在ホバリング中かどうか（重力無視フラグ）
//    float hoverAltitude = 20.0f;   // ジャンプ時に留まる上空の高さ（Y座標）
//
//    // --- 4バイト型 (float: ランタイム状態) ---
//    float currentMoveSpeed = 0.0f;
//    float dynamicJumpSpeed = 0.0f;
//    float actionTimer = 0.0f;
//
//    // --- 4バイト型 (int: 筋肉が知るべきフェーズ情報) ---
//    // ※特殊なアニメーション分岐等が必要になった時のために持たせておく
//    int currentPhase = 1;
//
//    // --- 1バイト型 (enum / bool) ---
//    BossActionState currentState = BossActionState::None;
//    bool jumpRequested = false;
//    bool isStrafingRight = false; // 現在右回りかどうかを記憶するフラグ
//
//    // 突進通路・突進攻撃の「ワールド起点座標」をロックするためのコンポーネントメモリ
//    DirectX::SimpleMath::Vector3 chargeAnchorPosition = DirectX::SimpleMath::Vector3::Zero;
//};