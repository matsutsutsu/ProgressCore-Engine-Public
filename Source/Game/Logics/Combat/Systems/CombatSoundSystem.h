#pragma once
// ===========================================================================
// 概要: 戦闘中の出来事（攻撃の命中・プレイヤーの被弾・ジャスト回避・ダッシュ）に合わせて効果音を鳴らす System
//
// 責務:
//   - DamageTakenEvent を受け、プレイヤーが受けたら「被弾」、それ以外（ボス）が受けたら「命中」を鳴らす
//   - JustEvadeSuccessEvent を受け、「ジャスト回避」と「スローの始まり」を鳴らす
//   - PlayerDashedEvent を受け、「ダッシュ」を鳴らす
//
// 備考: イベントを受けるだけにして、ダメージや移動の System が音を知らないようにする
//       素振りや着地のようにアニメのタイミングで決まる音は、アニメのシーケンス（Sound イベント）に置く
//       プレイヤーへの手応えなので、距離で小さくならないよう 2D で鳴らす。イベント名は Assets/Audio/SE の下のファイル名
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

class CombatSoundSystem : public CCL::ECS::IfSystem<
    CombatSoundSystem>
{
public:
    CombatSoundSystem() : IfSystem("CombatSoundSystem") {}

    void Initialize() override;
    void Update(float dt) override; // 鳴らすのはイベントを受けたときで、毎フレームの処理はない

private:
    // イベント名の効果音を1回鳴らす（音声がないシーンでは何もしない）
    // volume は音量の倍率（1 で等倍）
    void Play(const char *eventName, float volume);
};
