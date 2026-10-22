#pragma once
// ===========================================================================
// 概要: 画面左下にプレイヤーの HP ゲージを作り、HealthComponent に毎フレーム合わせる System。
//
// 責務:
//   - Initialize でゲージと残り HP の数値の UI エンティティを作る
//   - HP の割合と残り HP の数値を毎フレーム書く。色は白金の単色で、残りが少ないときだけ赤く脈打たせる
//   - 被弾したら、失った分（value〜trailValue）を少し見せてから遅れて減らし、ゲージを揺らす
//   - 被弾・回復・瀕死の演出（グリッチ・回復の光・外側の光）の進み具合を書く
//   - OnGui で演出を切り替え、HP をボタンで増減できる（見比べ用）
//
// 備考: 対象は HealthComponent と PlayerComponent を持つエンティティ。居ないシーンでは隠す。
//       自分の情報は左下、敵の情報は上辺に分ける。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Combat/Components/HealthComponent.h"
#include "Game/Logics/Character/Player/Core/PlayerComponent.h"

class PlayerHealthHudSystem : public CCL::ECS::IfSystem<PlayerHealthHudSystem,
                                                        CCL::ECS::Read<HealthComponent>,
                                                        CCL::ECS::Read<PlayerComponent>>
{
public:
    PlayerHealthHudSystem() : IfSystem("PlayerHealthHudSystem") {}

    void Initialize() override;
    void Update(float dt) override;
    void OnGui() override; // 演出の切り替えと、HP を増減するボタン（見比べ用）

private:
    CCL::ECS::EntityID m_barID   = CCL::ECS::InvalidEntityID; // ゲージ本体（UIProgressBar）
    CCL::ECS::EntityID m_valueID = CCL::ECS::InvalidEntityID; // 残り HP の数値（UIText。ゲージの右に小さく出す）

    float m_prevRatio = 1.0f;  // 前のフレームの HP の割合（被弾したかを見るため）
    float m_shake     = 0.0f;  // 被弾の揺れの残り（0〜1。毎フレーム減らす）
    float m_shakeTime = 0.0f;  // 揺れの経過時間（秒）。ゲームの dt で進めるのでポーズ中は止まる
    float m_trailHold = 0.0f;  // 被弾してから遅れて減るバーが動き出すまでの残り時間（秒）
    float m_pulseTime = 0.0f;  // 残り HP が少ないときの脈打ちの位相（秒）
    bool  m_init      = false; // ゲージの値を最初に合わせたか

    // --- 調整値 ---
    static constexpr float kTrailSpeed = 6.0f;  // 遅れて減るバーの追いつく速さ（大きいほど速い）
    static constexpr float kTrailHold  = 0.30f; // 被弾してから遅れて減るバーが動き出すまでの時間（秒）
    static constexpr float kShakeDecay = 9.0f;  // 揺れが収まる速さ（大きいほど短い）

    static constexpr float kLowHpStart = 0.40f; // この割合を下回ると赤へ寄せ始める
    static constexpr float kLowHpFull  = 0.25f; // この割合以下で完全に赤にする
    static constexpr float kPulseHz    = 1.6f;  // 残りが少ないときの脈打ちの速さ（回/秒）
};
