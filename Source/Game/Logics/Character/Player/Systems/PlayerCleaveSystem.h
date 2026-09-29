#pragma once
// ===========================================================================
// 概要: プレイヤーの特殊アクション「切り払い」を処理する System。
//
// 責務:
//   - 待機 → 準備 → 突進 のフェーズを進め、当たればチェインで準備に戻す
//   - 準備中は世界全体のスロー・画面のモノクロ・カメラの寄りを要求し、キャラを照準方向へ向ける
//   - 突進中は 3D 速度を上書きし、前方の扇にいるドローンを進行方向へ吹き飛ばす
//   - ジャスト回避とダッシュ開始でゲージを足し、ストックに変える
//
// 備考: 準備の猶予とクールダウンは実時間で進め、全体スロー中でも同じ秒数で切れるようにする。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Time/TimeState.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Game/Logics/Character/Player/Components/CleaveComponent.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"      // 照準方向へ向ける yaw を書く
#include "Game/Logics/Character/Player/Components/PlayerInputComponent.h" // 準備と突進の押下
#include "Game/Logics/Character/Player/Components/DashComponent.h"        // ダッシュ開始でゲージを足すために読む

// RootMotion は実行時に AnimSetupSystem が後から付け、無いこともあるので、前方宣言して GetComponent で扱う。
struct RootMotionComponent;

/**
 * @brief 切り払いのフェーズを進める System。
 *
 * 1. 右クリックで準備に入る
 * 2. 準備中に左クリックで突進する
 * 3. 突進でドローンに当たり、ストックが残っていれば再び準備に入る（チェイン）
 * 4. 空振りかストック切れで待機に戻り、クールダウンの間は準備に入れない
 *
 * @note 押した瞬間の判定は PlayerInputSystem が済ませている（PlayerInputComponent）。
 */
class PlayerCleaveSystem : public CCL::ECS::IfSystem<PlayerCleaveSystem,
                                                     CCL::ECS::Write<CharacterMovementComponent>,
                                                     CCL::ECS::Write<CleaveComponent>,
                                                     CCL::ECS::Write<PlayerComponent>,
                                                     CCL::ECS::Read<PlayerInputComponent>,
                                                     CCL::ECS::Read<DashComponent>,
                                                     CCL::ECS::Read<TransformComponent>>
{
public:
    PlayerCleaveSystem() : IfSystem("PlayerCleaveSystem") {}
    void Initialize() override;
    void Update(float dt) override;

private:
    // ゲージを足し、maxGauge を超えた分をストックに変える（上限 maxStock）。
    void AddCleaveGauge(CleaveComponent &cleave, float amount);
    // 準備(Ready)に入る。照準方向を書き、全体スローとモノクロを始める。
    void EnterReady(CleaveComponent &cleave, const DirectX::SimpleMath::Vector3 &dir);

    // チェイン段数（0/1/2 に丸める）に応じたスロー倍率と準備の猶予を返す。
    // enableStageDecay が false なら常に段1の値を返す。
    float CurrentSlowScale(const CleaveComponent &cleave) const;
    float CurrentReadyDuration(const CleaveComponent &cleave) const;

    // このフレームの全体スローを要求する（Time::SustainWorldSlow。プレイヤー自身も遅くなる）。
    // 呼ぶのをやめると slowDuration 秒で等速へ戻る。
    void HoldGlobalSlow(const CleaveComponent &cleave);
    // 全体スローをすぐに解除する（突進の瞬間に等速へ戻す）。
    void ClearGlobalSlow();
    // 画面のモノクロ演出の ON/OFF（JustEvadeEffectComponent の hold を使う）。
    void SetMonochrome(bool on);
    // プレイヤーを照準の水平方向へ向ける。
    void FaceAim(PlayerComponent &player, const DirectX::SimpleMath::Vector3 &dir);

    // このプレイヤーを映す TPS カメラへ、寄り（クローズアップ）の要求を書く。
    void PushCameraCloseUp(CCL::ECS::EntityID playerID, bool close);

    // 突進(Lunging)を始める。ストックを1消費し、スローとモノクロを解除する。
    void StartLunge(CleaveComponent &cleave, const DirectX::SimpleMath::Vector3 &dir);

    // 突進を終える。当たっていてストックが残っていれば再び準備に入り、そうでなければ待機に戻してクールダウンに入る。
    void EndLunge(CleaveComponent &cleave, RootMotionComponent *rm,
                  const DirectX::SimpleMath::Vector3 &aim);

    // 準備の猶予が切れたときに、待機(Idle)へ戻してクールダウンに入る。
    void ExitToIdle(CleaveComponent &cleave, RootMotionComponent *rm);

    // 前方の扇の中にいるドローンを進行方向へ吹き飛ばす（ドローンだけを走査するので、ボスは対象外）。
    void ApplyFanKnockback(const DirectX::SimpleMath::Vector3 &origin, CleaveComponent &cleave);
};
