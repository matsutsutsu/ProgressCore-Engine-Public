#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include <DirectXMath.h>
#include "Game/Logics/GameEvent/GameEvents.h"
#include "Game/Logics/Character/Player/Components/DashComponent.h"

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
//   - ダッシュ/接近の集中線（放射ブラー）
//   - ダメージSE（将来的に追加予定）
//
// 実際のポストエフェクト描画はPostProcessSystem側が担当し、
// 本Systemでは各Componentのパラメータを演出時間に応じて更新する。
//
// OnGui()では、ゲーム実行中に演出パラメータを調整できる。
// ===================================================================================
//
// Read<DashComponent> はダッシュ集中線のため。ダッシュ状態は PlayerDashSystem(L02)
// が確定させ、本システム(L06)は読むだけなのでステージの壁で直列化される。
class PlayerFeedbackSystem
    : public CCL::ECS::IfSystem<PlayerFeedbackSystem,
          CCL::ECS::Read<DashComponent>>
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

    // カメラには触れず CameraShakeRequestEvent を出すだけ。振幅・周波数・
    // 揺れる長さはカメラ側(CameraShake)の一元管理で、ここが持つのは
    // 「被弾のダメージ量をどれくらいの衝撃と見なすか」だけ。
    void StartDamageCameraShake(float damageAmount);

    // ダメージ量 → 衝撃量(trauma) の換算係数。
    //
    // 実際の揺れは trauma の2乗に比例する。0.3 と 0.6 では揺れが4倍違うので、
    // 「小さい被弾も一応揺れてほしい」なら下限を高めに取る必要がある。
    float m_traumaPerDamage = 0.07f;

    // 被弾時の衝撃量の下限（かすり傷でも最低これだけは揺らす）。
    float m_traumaMin = 0.45f;

    // 被弾時の衝撃量の上限（1.0 でカメラの最大の揺れ）。
    float m_traumaMax = 1.0f;

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

    // =========================================================================
    // Dash Radial Blur
    // =========================================================================

    // ダッシュ/接近の集中線を毎フレーム更新する。
    //
    // 専用タイマーを持たず DashComponent の残り時間から直接引く。
    // ヒットストップ等で dash 側のタイマーが止まれば集中線もそのまま固まる。
    void UpdateDashRadialBlur();

    // -------------------------------------------------------------------------
    // Dash Radial Blur Tuning
    // -------------------------------------------------------------------------

    // 回避ダッシュ中の集中線の強さ（開始直後が最大、終わりに向けて抜ける）。
    float m_dashBlurPeak = 0.35f;

    // ジャスト回避後の接近中の集中線の強さ。通常ダッシュより速いので強めが合う。
    float m_approachBlurPeak = 0.55f;
};
