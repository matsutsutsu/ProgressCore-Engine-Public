#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include <DirectXMath.h>
#include "Game/Logics/GameEvent/GameEvents.h"

// ===================================================================================
// PlayerFeedbackSystem
//
// プレイヤーに対するゲームプレイ上の「フィードバック演出」を管理する。
//
// DamageTakenEventを受け取った際に、以下の演出を開始する。
//   - カメラシェイク
//   - コントローラー振動
//   - 被弾ビネット
//   - 被弾色収差
//   - ダメージSE（将来的に追加予定）
//
// 実際のポストエフェクト描画はPostProcessSystem側が担当し、
// 本Systemでは各Componentのパラメータを演出時間に応じて更新する。
//
// OnGui()では、ゲーム実行中に演出パラメータを調整できる。
// ===================================================================================
class PlayerFeedbackSystem
    : public CCL::ECS::IfSystem<PlayerFeedbackSystem>
{
  public:
    PlayerFeedbackSystem();
    ~PlayerFeedbackSystem() override = default;

    void Initialize() override;

    void Update(float dt) override;

    void OnGui() override;

  private:
    // =========================================================================
    // Damage Feedback
    // =========================================================================

    // プレイヤーがダメージを受けた際の各種演出を開始する。
    void OnPlayerDamaged(const DamageTakenEvent &event);

    // プレイヤーの攻撃が敵に命中した時のフィードバック。
    void OnPlayerAttackHit(const AttackHitConfirmedEvent &event);

    void StartAttackHitVibration(const AttackHitConfirmedEvent &event);

    // -------------------------------------------------------------------------
    // Camera Shake
    // -------------------------------------------------------------------------

    void StartDamageCameraShake(float damageAmount);

    // ダメージ量 → カメラシェイク強度の倍率。
    float m_shakeDamageMultiplier = 0.1f;

    // カメラシェイクの最低強度。
    float m_shakeMinStrength = 0.2f;

    // カメラシェイクの最大強度。
    float m_shakeMaxStrength = 3.0f;

    // カメラシェイク継続時間。
    float m_shakeDuration = 0.5f;

    // -------------------------------------------------------------------------
    // Controller Vibration
    // -------------------------------------------------------------------------

    void StartDamageVibration();

    float m_vibrationLeft     = 0.5f;
    float m_vibrationRight    = 0.5f;
    float m_vibrationDuration = 0.2f;

    // =========================================================================
    // Attack Hit Vibration
    // =========================================================================

    float m_attackHitVibrationLeft     = 0.55f;
    float m_attackHitVibrationRight    = 0.55f;
    float m_attackHitVibrationDuration = 0.2f;
    float m_attackHitFeedbackScale     = 1.0f;


    // =========================================================================
    // Damage Vignette
    // =========================================================================

    // 被弾ビネットの演出を開始する。
    void StartDamageVignette();

    // 被弾ビネットを毎フレーム更新する。
    void UpdateDamageVignette(float dt);

    // 被弾ビネットの残り時間。素の設定の退避は持たない
    // （PostProcess のスロットへ目標値と weight を書くだけでよい）。
    float m_vignetteTimer = 0.0f;

    // -------------------------------------------------------------------------
    // Vignette Tuning
    // -------------------------------------------------------------------------

    // 被弾ビネットの継続時間。
    float m_vignetteDuration = 0.55f;

    // 被弾時のビネット強度。
    float m_vignettePeak = 0.95f;

    // 被弾時のビネットカラー。
    float m_vignetteR = 0.10f;
    float m_vignetteG = 0.03f;
    float m_vignetteB = 0.03f;

    // 被弾時のビネット形状。
    float m_vignetteRadius   = 0.15f;
    float m_vignetteSoftness = 0.60f;

    // =========================================================================
    // Damage Chromatic Aberration
    // =========================================================================

    // 被弾色収差の演出を開始する。
    void StartDamageChromaticAberration();

    // 被弾色収差を毎フレーム更新する。
    void UpdateDamageChromaticAberration(float dt);

    float m_chromaticTimer = 0.0f;

    // -------------------------------------------------------------------------
    // Chromatic Aberration Tuning
    // -------------------------------------------------------------------------

    // 色収差の継続時間。
    float m_chromaticDuration = 0.20f;

    // 被弾直後の色収差強度。
    float m_chromaticPeak = 0.2f;
};
