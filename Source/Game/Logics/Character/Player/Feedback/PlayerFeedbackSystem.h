#pragma once
// ===========================================================================
// 概要: プレイヤーの被弾・攻撃命中に合わせて、画面とコントローラーの演出を出す System。
//
// 責務:
//   - プレイヤーの被弾（DamageTakenEvent）で、カメラの揺れ・振動・ビネット・色収差を始める
//   - プレイヤーの攻撃命中（AttackHitConfirmedEvent）で、ダメージ量に応じた振動を出す
//   - OnGui で演出の値を実行中に調整できるようにする
//
// 備考:
//   ポストエフェクトは各 Config Component の上書きスロットへ目標値と weight を書くだけで、
//   描画と合成は PostProcessSystem が行う。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include <DirectXMath.h>
#include "Game/Core/GameEvents.h"
#include "Game/Logics/Character/Player/Core/PlayerComponent.h"

class PlayerFeedbackSystem
    : public CCL::ECS::IfSystem<PlayerFeedbackSystem,
                                CCL::ECS::Read<PlayerComponent>>
{
public:
    PlayerFeedbackSystem() : IfSystem("PlayerFeedbackSystem") {}
    ~PlayerFeedbackSystem() override = default;

    void Initialize() override;

    // テンプレート引数以外に書く型を申告する。
    //   ポストプロセスの設定は上書きスロットを通して書くので、申告しないと同じ設定を書く
    //   他の System と同じバッチに入って並列に走り、Acquire が同じ空きスロットを奪い合う。
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override;

    void Update(float dt) override;

    void OnGui() override;

private:
    // ============================================================
    // 被弾と攻撃命中
    // ============================================================

    // プレイヤーの被弾で、揺れ・振動・ビネット・色収差を始める。
    void OnPlayerDamaged(const DamageTakenEvent &event);

    // プレイヤーの攻撃が命中したときに振動を出す（攻撃者がプレイヤーでなければ何もしない）。
    void OnPlayerAttackHit(const AttackHitConfirmedEvent &event);

    void StartAttackHitVibration(const AttackHitConfirmedEvent &event);

    // ============================================================
    // カメラの揺れ
    // ============================================================

    /**
     * @brief 被弾時のカメラの揺れを要求する。
     *
     * カメラには触れず CameraShakeRequestEvent を出すだけ。
     * 振幅・周波数・揺れる長さはカメラ側（CameraShake）で決め、ここが持つのは被弾のダメージ量をどれくらいの衝撃（trauma）とみなすかだけ。
     *
     * @param damageAmount 受けたダメージ量。
     */
    void StartDamageCameraShake(float damageAmount);

    // ダメージ量から衝撃量(trauma)への換算係数。
    //
    // 実際の揺れは trauma の2乗に比例する。0.3 と 0.6 では揺れが4倍違うので、
    // 小さい被弾でも揺らしたいなら下限を高めにする。
    float m_traumaPerDamage = 0.07f;

    float m_traumaMin = 0.45f; // 被弾時の衝撃量の下限（小さい被弾でも最低これだけ揺らす）
    float m_traumaMax = 1.0f;  // 被弾時の衝撃量の上限（1.0 でカメラの最大の揺れ）

    // ============================================================
    // コントローラーの振動
    // ============================================================

    void StartDamageVibration();

    // 被弾時（モーターの強さ 0〜1、秒）
    float m_vibrationLeft     = 0.5f;
    float m_vibrationRight    = 0.5f;
    float m_vibrationDuration = 0.2f;

    // 攻撃命中時（モーターの強さ 0〜1、秒）。
    // m_attackHitFeedbackScale はダメージ量をこの値で割って振動の倍率（0.25〜1.0）にする
    float m_attackHitVibrationLeft     = 0.55f;
    float m_attackHitVibrationRight    = 0.55f;
    float m_attackHitVibrationDuration = 0.2f;
    float m_attackHitFeedbackScale     = 1.0f;

    // ============================================================
    // 被弾ビネット
    // ============================================================

    void StartDamageVignette();
    void UpdateDamageVignette(float dt);

    // 被弾ビネットの残り時間（秒）。元の設定は退避しない
    // （PostProcess のスロットへ目標値と weight を書くだけでよい）。
    float m_vignetteTimer = 0.0f;

    float m_vignetteDuration = 0.55f; // 継続時間（秒）
    float m_vignettePeak     = 0.95f; // 被弾直後の強さ

    // 色
    float m_vignetteR = 0.10f;
    float m_vignetteG = 0.03f;
    float m_vignetteB = 0.03f;

    // 形状
    float m_vignetteRadius   = 0.15f;
    float m_vignetteSoftness = 0.60f;

    // ============================================================
    // 被弾色収差
    // ============================================================

    void StartDamageChromaticAberration();
    void UpdateDamageChromaticAberration(float dt);

    float m_chromaticTimer    = 0.0f;  // 残り時間（秒）
    float m_chromaticDuration = 0.20f; // 継続時間（秒）
    float m_chromaticPeak     = 0.2f;  // 被弾直後の強さ
};
