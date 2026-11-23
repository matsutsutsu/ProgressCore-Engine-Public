#pragma once

// HP と無敵時間
// 敵か味方かは持たない。命中の敵味方は物理レイヤー（PLAYER_ATTACK / BOSS_ATTACK）が分ける
struct HealthComponent
{
    float maxHealth             = 100.0f;
    float currentHealth         = 100.0f;
    float invincibilityTimer    = 0.0f; // 無敵の残り時間（秒）。DamageResolutionSystem が始め、HealthSystem が減らす
    float invincibilityDuration = 0.5f; // 被弾したときの無敵の長さ（秒）

    // とどめ以外の攻撃では、HP をこれより下げない（保存しない）
    // ボスは BossPhaseSystem が毎フレーム書く。0 なら下限なし
    float finisherFloor = 0.0f;

    // デバッグ: 当たっても HP を減らさない（保存しない）。Boss Debugger が切り替える
    // 当たった演出・被弾の通知（怒りの蓄積）はそのまま出る
    bool debugNoDamage = false;
};