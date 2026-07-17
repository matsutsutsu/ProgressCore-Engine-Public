#pragma once
/// @file OverdriveComponent.h
/// @brief OVERDRIVE（ゲージ→発動→自分だけ高速の無双状態）のプレイヤー状態を保持する。
///
/// 企画上の位置付け:
///   - 回避（ダッシュ）とジャスト回避でゲージを蓄積し、MAXで任意発動。
///   - 発動中は「世界がゆっくり・プレイヤーだけ高速」になる大火力/殲滅の時間。
///   - ジャスト回避チェインの削り（腕前で積む火力）に対し、ゲージという資源で
///     保証された確実な火力の軸を担う。
///
/// 設計方針:
///   - このコンポーネントは「状態の保持」のみ。時間スケールの適用は OverdriveSystem が
///     TimeContext(overdrive*Scale) を通して行い、TimeScaleSystem が実効dtへ反映する。
struct OverdriveComponent
{
    // --- ゲージ（チューニング） ---
    float maxGauge        = 100.0f; ///< ゲージ最大値
    float gainOnJustEvade = 25.0f;  ///< ジャスト回避成立で加算（近道・多め）
    float gainOnDash      = 4.0f;   ///< 通常回避(ダッシュ)で僅かに加算＝詰み対策の救済ルート

    // --- 発動（チューニング） ---
    float duration           = 6.0f;   ///< 発動持続(実秒)。MAXから0まで時間経過で減少。
    float enemyTimeScale      = 0.35f;  ///< 発動中の敵/ドローン(TimeGroup::Enemy)の時間倍率
    float environmentTimeScale = 0.35f; ///< 発動中の環境/ステージ/パーティクル(TimeGroup::Environment)の時間倍率
    float playerTimeScale     = 1.15f;  ///< 発動中のプレイヤー(TimeGroup::Player)の時間倍率(>1で高速)

    // --- ランタイム状態（非シリアライズ） ---
    float currentGauge = 0.0f; ///< 現在のゲージ量
    bool  isActive     = false; ///< OVERDRIVE 発動中か（Combat 側が DPS/一撃撃破の判定に読む想定）

    // --- 内部（非シリアライズ：エッジ検出用） ---
    bool wasDashing = false; ///< 前フレームのダッシュ状態（ダッシュ開始の立ち上がり検出）

    bool IsFull() const { return currentGauge >= maxGauge; }

    void AddGauge(float v)
    {
        currentGauge += v;
        if (currentGauge > maxGauge) currentGauge = maxGauge;
        if (currentGauge < 0.0f)     currentGauge = 0.0f;
    }
};
