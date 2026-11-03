#pragma once
// ===========================================================================
// 概要: 画面左下にプレイヤーの HP ゲージを出し、HealthComponent に毎フレーム合わせる System。
//
// 責務:
//   - プレイヤーが居るシーンで、PlayerHud プレハブを生成する
//   - HP の割合と残り HP の数値を書き、少ないときだけ赤く脈打たせる
//   - 被弾・回復の演出（遅れて減るバー・揺れ・回復の光）を進める
//
// 備考: 見た目は PlayerHud プレハブ、振る舞いはルートの PlayerHudStyleComponent が持つ。
//       プレハブには照準の調整値（子 LockOnMarker）も入っている。
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

    void Update(float dt) override;
    void OnGui() override; // HP を増減するボタン（見た目を確かめる用）

private:
    // PlayerHud プレハブを生成し、ゲージと数値の子を名前で引く
    void SpawnHud();

    CCL::ECS::EntityID m_rootID  = CCL::ECS::InvalidEntityID; // プレハブのルート（Canvas と PlayerHudStyle）
    CCL::ECS::EntityID m_barID   = CCL::ECS::InvalidEntityID; // ゲージ（UIProgressBar）
    CCL::ECS::EntityID m_valueID = CCL::ECS::InvalidEntityID; // 残り HP の数値（UIText）
    bool               m_spawned = false;                     // 生成を試したか

    float m_prevRatio = 1.0f;  // 前のフレームの HP の割合
    float m_shake     = 0.0f;  // 被弾の揺れの残り（0〜1）
    float m_shakeTime = 0.0f;  // 揺れの経過時間（秒）
    float m_trailHold = 0.0f;  // 遅れて減るバーが動き出すまでの残り時間（秒）
    float m_pulseTime = 0.0f;  // 瀕死の脈打ちの位相（秒）
    bool  m_init      = false; // ゲージの値を最初に合わせたか
};
