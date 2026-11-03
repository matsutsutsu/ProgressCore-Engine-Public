#pragma once
// ===========================================================================
// 概要: 画面左下にプレイヤーの HP ゲージを出し、HealthComponent に毎フレーム合わせる System。
//
// 責務:
//   - プレイヤーが居るシーンで、PlayerHud プレハブ（ゲージと残り HP の数値）を生成する
//   - HP の割合と残り HP の数値を毎フレーム書く。色は単色で、残りが少ないときだけ赤く脈打たせる
//   - 被弾したら、失った分（value〜trailValue）を少し見せてから遅れて減らし、ゲージを揺らす
//   - 被弾・回復・瀕死の演出（グリッチ・回復の光・外側の光）の進み具合を書く
//   - OnGui で HP をボタンで増減し、被弾・回復・瀕死の見た目を確かめられる
//
// 備考: 見た目と配置はプレハブ（Assets/Prefabs/UI/PlayerHud.json）が持つ。
//       エディタで PlayerHud を選んでインスペクタで調整し、インスペクタのプレハブ保存で残す。
//       振る舞いの調整値はプレハブのルートの PlayerHudStyleComponent。
//       プレハブには照準の調整値（子 LockOnMarker）も入っている。読むのは LockOnMarkerHudSystem。
//       対象は HealthComponent と PlayerComponent を持つエンティティ。居ないシーンでは作らない。
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
    CCL::ECS::EntityID m_barID   = CCL::ECS::InvalidEntityID; // ゲージ本体（UIProgressBar）
    CCL::ECS::EntityID m_valueID = CCL::ECS::InvalidEntityID; // 残り HP の数値（UIText。ゲージの右に小さく出す）
    bool               m_spawned = false;                     // 生成を試したか（失敗しても毎フレーム読み直さない）

    float m_prevRatio = 1.0f;  // 前のフレームの HP の割合（被弾したかを見るため）
    float m_shake     = 0.0f;  // 被弾の揺れの残り（0〜1。毎フレーム減らす）
    float m_shakeTime = 0.0f;  // 揺れの経過時間（秒）。ゲームの dt で進めるのでポーズ中は止まる
    float m_trailHold = 0.0f;  // 被弾してから遅れて減るバーが動き出すまでの残り時間（秒）
    float m_pulseTime = 0.0f;  // 残り HP が少ないときの脈打ちの位相（秒）
    bool  m_init      = false; // ゲージの値を最初に合わせたか
};
