#pragma once
#include <SimpleMath.h>

// ダッシュのパラメータと状態をひとまとめに管理するコンポーネント
struct DashComponent {
    // --- Config (プランナー調整用) ---
    float dashSpeed = 40.0f;
    float dashDuration = 0.15f;
    float dashCooldown = 0.5f;

    // ジャスト回避の「シビアさ」を決める受付時間
    float justEvadeWindow = 0.08f;

    // 回避の無敵時間。isInvincible が立っている間、被弾処理が実際に見る
    // HealthComponent.invincibilityTimer を毎フレームこの値で維持し、ダメージ/ドローン接触を
    // すり抜けさせる。回避（ダッシュ/接近）が終わってもこの秒数だけ無敵が残る（着地保険）。
    float invincibleWindow = 0.06f;

    // チェイン段数による受付窓の倍率(1.0=通常, 小さいほど厳しい)。PlayerChain 側が毎フレーム設定する。
    float justEvadeWindowScale = 1.0f;

    // --- 接近 (ジャスト回避スロー中にボスへ詰める「接近専用時間」) Config ---
    // 狙い: ジャスト回避成立で短いスローが発生し、その間にボスへ接近できる（殴れる位置に着くための時間）。
    //       大火力を出す時間ではなく、あくまで距離を詰めるための時間。
    float approachSpeed       = 60.0f; // 接近時の移動速度（通常ダッシュより速い「詰め」）
    float approachDuration    = 0.35f; // 接近を行う実時間(秒)。ヒットストップ明け後から計測する。
    float approachPlayerScale = 1.0f;  // 接近中、世界スローを打ち消してプレイヤーを動かす実効時間倍率(1.0=実時間, >1で高速)
    float approachStopRange   = 4.0f;  // ボスにこの距離まで近づいたら接近終了（めり込み防止）

    // --- State (実行時の状態) ---
    bool isDashing = false;
    bool isInvincible = false;

    // 現在、ジャスト回避の受付中（超シビアな時間帯）か？
    bool isJustEvadeWindowActive = false;

    // 接近中か？ / 接近の残り時間
    bool  isApproaching = false;
    float approachTimer = 0.0f;

    float currentTimer = 0.0f;
    DirectX::SimpleMath::Vector3 dashDirection = { 0, 0, 0 };
};