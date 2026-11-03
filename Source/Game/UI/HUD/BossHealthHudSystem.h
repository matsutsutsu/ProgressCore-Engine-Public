#pragma once
// ===========================================================================
// 概要: 画面上部にボスの HP ゲージと名前・ドローンの残り数を出し、毎フレーム合わせる System。
//
// 責務:
//   - ボスが初めて出たときに、BossHud プレハブ（ゲージ・名前・ドローン数）を生成する
//   - HP の割合・被弾したときの揺れ・遅れて減るバー（失った分の表示）を毎フレーム書く
//   - 第2形態へ移る HP の位置にゲージの目印の線を引き、そこを過ぎたら消す
//   - BattleContext のドローンの生存数を、ゲージの下に書く
//   - ボスが居ないとき・倒したときはまとめて隠す
//   - OnGui で HP をボタンで増減し、被弾・回復・第2形態の見た目を確かめられる
//
// 備考: 見た目と配置はプレハブ（Assets/Prefabs/UI/BossHud.json）が持つ。
//       エディタで BossHud を選んでインスペクタで調整し、インスペクタのプレハブ保存で残す。
//       振る舞いの調整値はプレハブのルートの BossHudStyleComponent。
//       対象は HealthComponent と BossActionComponent を持ち、HP が 0 より大きいボス。
//       ドローンを減らす経路はまだ無い（DroneManagerSystem の備考）ので、今は数が初期配備数のまま動かない。
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
    CCL::ECS::EntityID m_barID   = CCL::ECS::InvalidEntityID; // ゲージ本体（UIProgressBar）
    CCL::ECS::EntityID m_nameID  = CCL::ECS::InvalidEntityID; // ボスの名前（UIText。ゲージの左上）
    CCL::ECS::EntityID m_swarmID = CCL::ECS::InvalidEntityID; // ドローンの残り数（UIText。ゲージの左下）
    bool               m_spawned = false;                     // 生成を試したか（失敗しても毎フレーム読み直さない）

    float m_prevRatio = 1.0f;  // 前のフレームの HP の割合（被弾したかを見るため）
    float m_shake     = 0.0f;  // 被弾の揺れの残り（0〜1。毎フレーム減らす）
    float m_shakeTime = 0.0f;  // 揺れの経過時間（秒）。ゲームの dt で進めるのでポーズ中は止まる
    float m_trailHold = 0.0f;  // 被弾してから遅れて減るバーが動き出すまでの残り時間（秒）
    bool  m_init      = false; // ゲージの値を最初に合わせたか
};
