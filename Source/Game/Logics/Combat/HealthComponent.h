#pragma once

// 【純粋データ】HPと無敵状態の保持のみを行う。
// 陣営(TeamID)は撤去。命中の敵味方判定は物理レイヤー(PLAYER_ATTACK/BOSS_ATTACK)で行う。
struct HealthComponent
{
    float maxHealth             = 100.0f;
    float currentHealth         = 100.0f;
    float invincibilityTimer    = 0.0f;
    float invincibilityDuration = 0.5f;

    // 撃破スロー演出を「1体につき1回だけ」発火させるための実行時フラグ（非シリアライズ）。
    // DefeatSlowMotionSystem が死亡検知時に true にする。
    bool deathFxFired = false;
};