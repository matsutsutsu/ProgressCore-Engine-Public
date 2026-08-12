#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Time/TimeState.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementInputComponent.h"
#include "Game/Logics/Character/Player/Components/CleaveComponent.h"

// ポインタ参照のみのため前方宣言で足りる。
struct RootMotionComponent;
struct PlayerComponent;

/// @brief 切り払い(CLEAVE)：右クリックで「準備(スロー)」→左クリックで突進→当てれば再準備で連鎖。
///        突進速度は PlayerDashSystem(L02_Update) の後（L02_PostUpdate）で overrideSpeed を
///        上書きするので、ダッシュと取り合わずに確実に効く。吹き飛ばしは DroneMovementSystem
///        (L03_PrePhysics) より前に書けるので同フレームで反映される。
///        準備/突進中の左クリックは PlayerAttackSystem が通常攻撃を抑止する（phase を参照）。
class PlayerCleaveSystem : public CCL::ECS::IfSystem<
    PlayerCleaveSystem,
    CCL::ECS::Write<CharacterMovementInputComponent>,
    CCL::ECS::Write<CleaveComponent>,
    CCL::ECS::Read<TransformComponent>>
{
public:
    PlayerCleaveSystem() : IfSystem("PlayerCleaveSystem") {}
    void Update(float dt) override;

private:
    /// 準備(Ready)へ入る（照準方向をセットし、全体スロー＋モノクロを開始）。
    void EnterReady(CleaveComponent &cleave, const DirectX::SimpleMath::Vector3 &dir);

    /// チェイン段数に応じて減衰した現在のスロー倍率／準備猶予（企画_03 3.1.1）。
    float CurrentSlowScale(const CleaveComponent &cleave) const;
    float CurrentReadyDuration(const CleaveComponent &cleave) const;

    /// ボス(BossActionComponent)の位置を取得（不在なら false）。前進判定に使う。
    bool TryGetBossPos(DirectX::SimpleMath::Vector3 &out) const;

    /// 準備中：全体スロー(cinematicSlow=globalScale経由で自機含む)を維持する。
    void HoldGlobalSlow(const CleaveComponent &cleave);
    /// スローを即解除（突進の瞬間に等速へスナップ）。
    void ClearGlobalSlow();
    /// 画面モノクロ演出の ON/OFF（ジャスト回避の演出singletonを hold で流用）。
    void SetMonochrome(bool on);
    /// 照準方向へプレイヤーを向ける。
    void FaceAim(PlayerComponent &player, const DirectX::SimpleMath::Vector3 &dir);

    /// このプレイヤーを映す TPS カメラへ「寄り(クローズアップ)」要求を push する。
    void PushCameraCloseUp(CCL::ECS::EntityID playerID, bool close);

    /// 突進(Lunging)を開始。突進開始時のボス距離を記録する（前進判定用）。
    void StartLunge(CleaveComponent &cleave, const DirectX::SimpleMath::Vector3 &dir,
                    const DirectX::SimpleMath::Vector3 &playerPos);

    /// 突進終了：ヒットかつボスへ前進していれば再準備で連鎖、そうでなければ解除しクールダウンへ。
    void EndLunge(CleaveComponent &cleave, CharacterMovementInputComponent &input,
                  RootMotionComponent *rm, const DirectX::SimpleMath::Vector3 &aim,
                  const DirectX::SimpleMath::Vector3 &playerPos);

    /// 待機(Idle)へ戻しクールダウンを張る（時間切れ・空振り時）。
    void ExitToIdle(CleaveComponent &cleave, CharacterMovementInputComponent &input, RootMotionComponent *rm);

    /// 前方の扇内のドローンを進行方向へ吹き飛ばす（ボスは対象外＝ドローンのみ走査）。
    void ApplyFanKnockback(const DirectX::SimpleMath::Vector3 &origin, CleaveComponent &cleave);
};
