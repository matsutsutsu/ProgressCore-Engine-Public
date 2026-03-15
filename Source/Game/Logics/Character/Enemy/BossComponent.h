#pragma once
#include "ECS/Common/CCL_Common.h"

// ボスの行動ステート
enum class BossState {
    Intro,       // 登場演出（今回はスキップしてIdleへ）
    Idle,        // 待機・様子見
    Move,        // プレイヤーへ歩く
    Charge,      // 突進（Tankの強化版）
    SweepAttack, // 薙ぎ払い（近距離範囲攻撃）
    Summon,      // 雑魚召喚
    Shockwave,   // 衝撃波（ジャンプ回避強要）
    Stun,        // フェーズ移行時の硬直
    Death,       // 死亡
};

struct BossComponent {
    // --- ステート管理 ---
    BossState state      = BossState::Idle;
    float     stateTimer = 0.0f;

    // --- フェーズ管理 ---
    int  currentPhase = 1;     // 1:通常 -> 2:発狂
    bool isEnraged    = false; // 発狂フラグ

    // --- パラメータ ---
    float moveSpeed        = 5.0f;  // 通常移動は遅く重厚に
    float chargeSpeed      = 25.0f; // 突進は高速に
    float rotationSpeed    = 2.0f;  // 旋回速度
    float attackRangeSweep = 6.0f;  // 薙ぎ払いの射程
    float attackRangeShock = 25.0f; // 衝撃波の射程

    // --- クールダウン管理 ---
    float actionCooldown    = 2.0f;  // 行動後の隙
    float summonCooldown    = 0.0f;  // 召喚CT
    float shockwaveCooldown = 10.0f; // 衝撃波CT

    // ---  Luaに渡すための情報 ---
    float distanceToPlayer = 0.0f; // 毎フレームC++が計算してここに格納する

    // --- 召喚管理 ---
    int maxMinions     = 3; // 盤面にいられる最大雑魚数
    int currentMinions = 0; // 現在の雑魚数（死んだら減らす処理が必要）

    // フラグ
    bool hasActioned = false;

    // 外部（GameScene）から死亡を判定するためのフラグ
    bool isDeadAnimationFinished = false;

    bool hasHitTarget = false; // ★追加: 突進などの接触判定用（1回だけ当たるようにする）

    // ========================================================================
    // アニメーションタイミング設定 (秒単位)
    // ========================================================================

    // 突進 (Charge)
    float chargeWindupDur   = 2.20f; // 予備動作（溜め）
    float chargeDashDur     = 0.80f; // 突進している時間
    float chargeRecoveryDur = 1.50f; // 停止後の硬直

    // 薙ぎ払い (Sweep)
    float sweepHitTime  = 0.75f; // 判定発生タイミング
    float sweepDuration = 1.50f; // 全体の長さ

    // 召喚 (Summon)
    float summonSpawnTime = 1.00f; // 召喚発生タイミング
    float summonDuration  = 2.00f; // 全体の長さ

    // 衝撃波 (Shockwave)
    float shockwaveHitTime  = 0.75f; // 判定発生タイミング
    float shockwaveDuration = 1.50f; // 全体の長さ

    // ========================================================================
    // ★追加: 攻撃力設定 (Player MaxHP = 100 想定)
    // ========================================================================
    float damageSweep     = 25.0f; // 薙ぎ払い
    float damageCharge    = 15.0f; // 突進
    float damageShockwave = 15.0f; // 衝撃波
};