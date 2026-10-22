#pragma once
// ===========================================================================
// 概要: 戦闘中の出来事（攻撃の命中・プレイヤーの被弾・ジャスト回避・ダッシュ）に合わせて効果音を鳴らす System。
//
// 責務:
//   - DamageTakenEvent を受け、プレイヤーが受けたら「被弾」、それ以外（ボス）が受けたら「命中」を鳴らす
//   - JustEvadeSuccessEvent を受け、「ジャスト回避」を鳴らす
//   - PlayerDashedEvent を受け、「ダッシュ」を鳴らす
//
// 備考:
//   イベントを受けるだけにして、ダメージや移動の System が音を知らないようにする（PlayerActionVfxSystem と同じ形）。
//   素振り・ボスの着地・突進のように、アニメのタイミングで決まる音はここではなく
//   アニメのシーケンス（Sound イベント）に置く。ここは「当たった」「避けた」のように、アニメからは分からない音だけを持つ。
//
//   どれもプレイヤーへの手応えなので、距離で小さくならないよう 2D で鳴らす。
//   イベント名とファイルの対応は Assets/Config/AudioConfigDXTK.json に書く。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

class CombatSoundSystem : public CCL::ECS::IfSystem<CombatSoundSystem>
{
public:
    CombatSoundSystem() : IfSystem("CombatSoundSystem") {}

    void Initialize() override;     // DamageTakenEvent / JustEvadeSuccessEvent / PlayerDashedEvent を購読する
    void Update(float dt) override; // 毎フレームの処理は無い（鳴らすのはイベントを受けたとき）

private:
    void Play(const char *eventName); // イベント名の効果音を1回鳴らす（音声が無いシーンでは何もしない）
};
