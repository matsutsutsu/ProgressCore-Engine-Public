#pragma once
// ===========================================================================
// 概要: 画面上部にボスの HP ゲージと名前・ドローンの残り数を出し、毎フレーム合わせる System。
//
// 責務:
//   - ボスが初めて出たときに、BossHud プレハブを生成する
//   - HP の割合と被弾の演出（遅れて減るバー・揺れ）を書く
//   - 第2形態へ移る位置の目印と、第2形態の外側の光を書く
//   - ドローンの生存数を書き、ボスが居ないときはまとめて隠す
//
// 備考: 見た目は BossHud プレハブ、振る舞いはルートの BossHudStyleComponent が持つ。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Combat/Components/HealthComponent.h"
#include "Game/Logics/Character/Boss/Components/BossActionComponent.h"

class BossHealthHudSystem : public CCL::ECS::IfSystem<BossHealthHudSystem,
                                                      CCL::ECS::Read<HealthComponent>,
                                                      CCL::ECS::Read<BossActionComponent>>
{
public:
    BossHealthHudSystem() : IfSystem("BossHealthHudSystem") {}

    void Update(float dt) override;
    void OnGui() override; // HP を増減するボタン（見た目を確かめる用）

private:
    // BossHud プレハブを生成し、ゲージ・名前・ドローン数の子を名前で引く
    void SpawnHud();

    // ゲージ以外の部品（名前・ドローン数）をまとめて隠す・出す
    void SetDecorVisible(bool visible);

    CCL::ECS::EntityID m_rootID  = CCL::ECS::InvalidEntityID; // プレハブのルート（Canvas と BossHudStyle）
    CCL::ECS::EntityID m_barID   = CCL::ECS::InvalidEntityID; // ゲージ（UIProgressBar）
    CCL::ECS::EntityID m_nameID  = CCL::ECS::InvalidEntityID; // ボスの名前（UIText）
    CCL::ECS::EntityID m_swarmID = CCL::ECS::InvalidEntityID; // ドローンの残り数（UIText）
    bool               m_spawned = false;                     // 生成を試したか

    float m_prevRatio = 1.0f;  // 前のフレームの HP の割合
    float m_shake     = 0.0f;  // 被弾の揺れの残り（0〜1）
    float m_shakeTime = 0.0f;  // 揺れの経過時間（秒）
    float m_trailHold = 0.0f;  // 遅れて減るバーが動き出すまでの残り時間（秒）
    bool  m_init      = false; // ゲージの値を最初に合わせたか
};
