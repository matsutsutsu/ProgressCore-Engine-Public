#pragma once
#include "ECS/Common/CCL_Common.h"

// ===================================================================================
// ファイル名: EnemyComponent.h
// クラス名: EnemyComponent (Component)
//
// 【役割・責任】
//   ・敵キャラクターの「思考（AI）」と「性格（ステータス）」を保持するデータコンポーネント
//   ・ステートマシン（現在何をしているか）とステアリング（どう動くか）のパラメータを一元管理する
//
// 【仕様】
//   1. 状態管理 (State Machine):
//      - Idle: 待機・索敵中
//      - Chase: 追跡移動中（ステアリング挙動が有効）
//      - Attack: 攻撃動作中
//      - Stun: 硬直中
//
//   2. 性格設定 (AI Type & Weights):
//      - Chaser: 突撃型（Seek優先）
//      - Repulser: 拒絶型（距離を取る）
//      - Phalanx: 集団行動型
//      - weights: 移動計算時の「追跡」「分離」「壁回避」の優先度を調整する重み係数
//
//   3. 戦闘パラメータ:
//      - 索敵範囲、攻撃射程、攻撃クールダウンなどを管理
// ===================================================================================


// 状態の拡張
enum class EnemyState : uint8_t {
    Idle,
    Chase,  // 通常移動
    Attack, // 近接攻撃
    Stun,   // 硬直
    Charge,     // タンクの突進
    Jump,       // ジャンパーの跳躍中
    Dodge,      // エベーダーの回避
    RangeAttack // ファイアの射撃
};

// 敵タイプの拡張
enum class EnemyAIType : uint8_t {
    Pawn,   // 一般兵
    Tank,   // タンク
    Jumper, // ジャンパー
    Evader, // 回避兵
    Boid,   // 小型群衆
    Fire    // 遠距離
};

struct EnemyComponent {
    // --- 状態管理 ---
    EnemyState  currentState = EnemyState::Idle;
    EnemyAIType aiType       = EnemyAIType::Pawn;
    float       stateTimer   = 0.0f; // 状態遷移してからの時間

    // --- 基本パラメータ ---
    float moveSpeed   = 8.0f;
    float detectRange = 20.0f; // 発見距離
    float attackRange = 2.0f;  // 攻撃開始距離

    // --- 個体差用 ---
    float speedRandomness = 1.0f; // 0.8~1.2などでバラつかせる
    float noiseOffset     = 0.0f; // 動きの揺らぎ用
    int   formationSlot   = 0;    // フローフィールドのスロット番号

    // --- 特殊行動用パラメータ ---
    float chargeSpeed     = 20.0f; // Tank: 突進速度
    float jumpForce       = 10.0f; // Jumper: 跳躍力(Y軸)
    float dodgeCooldown   = 0.0f;  // Evader: 回避クールダウン管理
    float rangeAttackDist = 15.0f; // Fire: 射撃距離

    // ★追加: Tankの突進用クールダウン
    float chargeCooldown     = 0.0f; // 現在の残り時間
    float chargeInterval     = 5.0f; // 次に使えるまでの時間
    float chargeWindUpTime   = 0.8f; // タメ時間（震える時間）
    float chargeDuration     = 1.0f; // 突進している時間
    float chargeRecoveryTime = 1.5f; // 突進後の硬直時間

    // ★追加: Jumperのジャンプ用クールダウン
    float jumpCooldown = 0.0f; // 現在の残り時間
    float jumpInterval = 2.2f; // 次に飛べるまでの時間（2秒くらい）

    // --- ステアリングの重み（性格付け） ---
    float weightSeek       = 1.0f; // ターゲットへ向かう強さ
    float weightSeparation = 1.5f; // 仲間と離れる強さ
    float weightWall       = 5.0f; // 壁を避ける強さ（高めに設定）

    float weightAlign      = 0.0f; // Boid用：仲間と同じ方向を向く

    // Repulser用
    float keepDistance = 10.0f;


    // --- 攻撃管理 ---
    float attackCooldown = 0.0f; // 現在のクールダウン残り時間
    float attackInterval = 1.5f; // 攻撃間隔（秒）
    bool  hasActioned    = false; // 突進や発射を1回だけ行うためのフラグ

    // ===========================================
    // 戦闘用パラメータ (Combat Stats)
    // ===========================================
    float baseDamage          = 10.0f; // 基礎攻撃力
    float knockbackPower      = 15.0f; // プレイヤーに与えるノックバック力
    float knockbackResistance = 0.0f;  // ノックバック耐性 (0.0:全受け ～ 1.0:完全無効)

    // --- ドロップアイテム設定 ---
    int dropMoney = 10; // デフォルトは10コイン

    // メタAI管理用フラグ
    bool hasAttackToken = false; // 現在攻撃権を持っているか

    // 旋回攻撃用フラグ
    bool isOrbiting = false;
};