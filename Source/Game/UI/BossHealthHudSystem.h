#pragma once
// ===========================================================================
// 概要: 画面上部にボスの HP ゲージと名前・ドローンの残り数を作り、毎フレーム合わせる System。
//
// 責務:
//   - Initialize でゲージ・名前・ドローン数の UI エンティティを作る
//   - HP の割合・被弾したときの揺れ・遅れて減るバー（失った分の表示）を毎フレーム書く
//   - 第2形態へ移る HP の位置にゲージの目印の線を引き、そこを過ぎたら消す
//   - BattleContext のドローンの生存数を、ゲージの下に書く
//   - 被弾・回復・第2形態の演出（グリッチ・回復の光・外側の光）の進み具合を書く
//   - ボスが居ないとき・倒したときはまとめて隠す
//   - OnGui で演出を切り替え、HP をボタンで増減できる（見比べ用）
//
// 備考: 対象は HealthComponent と BossActionComponent を持ち、HP が 0 より大きいボス。
//       ゲージの見た目は UIProgressBar が持ち、この System は値を渡すだけ（PlayerHealthHudSystem と同じ形）。
//       ドローンを減らす経路はまだ無い（DroneManagerSystem の備考）ので、今は数が初期配備数のまま動かない。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Combat/HealthComponent.h"
#include "Game/Logics/Character/Boss/Components/BossActionComponent.h"

class BossHealthHudSystem : public CCL::ECS::IfSystem<BossHealthHudSystem,
                                                      CCL::ECS::Read<HealthComponent>,
                                                      CCL::ECS::Read<BossActionComponent>>
{
public:
    BossHealthHudSystem()
        : IfSystem("BossHealthHudSystem")
    {
    }

    void Initialize() override;
    void Update(float dt) override;
    void OnGui() override; // 演出の切り替えと、HP を増減するボタン（見比べ用）

private:
    // ゲージ以外の部品（名前・ドローン数）をまとめて隠す・出す
    void SetDecorVisible(bool visible);

    CCL::ECS::EntityID m_barID   = CCL::ECS::InvalidEntityID; // ゲージ本体（UIProgressBar）
    CCL::ECS::EntityID m_nameID  = CCL::ECS::InvalidEntityID; // ボスの名前（UIText。ゲージの左上）
    CCL::ECS::EntityID m_swarmID = CCL::ECS::InvalidEntityID; // ドローンの残り数（UIText。ゲージの左下）

    float m_prevRatio = 1.0f;  // 前のフレームの HP の割合（被弾したかを見るため）
    float m_shake     = 0.0f;  // 被弾の揺れの残り（0〜1。毎フレーム減らす）
    float m_shakeTime = 0.0f;  // 揺れの経過時間（秒）。ゲームの dt で進めるのでポーズ中は止まる
    float m_trailHold = 0.0f;  // 被弾してから遅れて減るバーが動き出すまでの残り時間（秒）
    bool  m_init      = false; // ゲージの値を最初に合わせたか

    // --- 調整値 ---
    static constexpr float kTrailSpeed = 5.0f;  // 遅れて減るバーの追いつく速さ（大きいほど速い）
    static constexpr float kTrailHold  = 0.35f; // 被弾してから遅れて減るバーが動き出すまでの時間（秒）
    static constexpr float kShakeDecay = 9.0f;  // 揺れが収まる速さ（大きいほど短い）
};
