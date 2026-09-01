#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Time/TimeState.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Game/Logics/Character/Player/Components/CleaveComponent.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h" // Archetype条件（回頭に書く）
#include "Game/Logics/Character/Player/Components/PlayerInputComponent.h" // 準備/発動の押下
#include "Game/Logics/Character/Player/Components/DashComponent.h"   // Archetype条件（救済ゲージで読む）

// RootMotion は実行時後付け(AnimSetupSystem)＝任意依存なので前方宣言＋GetComponentで扱う。
struct RootMotionComponent;


/**
 * @brief プレイヤーの特殊アクション切り払い（CLEAVE）の処理を行うシステム
 * @details
 * 1. プレイヤーが右クリックをすると準備状態になる
 * 2. 準備状態で左クリックをすると突進状態になる
 * 3. 突進状態で敵に当たると再び準備状態になり連鎖が続く
 * 4. 突進が終了するとクールダウン状態になり、一定時間後に再び準備状態に戻る
 * ※ 押下の立ち上がり判定は PlayerInputSystem が済ませている（PlayerInputComponent）。
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
    // ゲージを加算し、満タンぶんをストックへ変換する（最大3ストック）。
    void AddCleaveGauge(CleaveComponent &cleave, float amount);
    // 準備(Ready)へ入る（照準方向をセットし、全体スロー＋モノクロを開始）。
    void EnterReady(CleaveComponent &cleave, const DirectX::SimpleMath::Vector3 &dir);

    // チェイン段数(0/1/2)に応じた段ごとの固定スロー倍率／準備猶予を返す。
    float CurrentSlowScale(const CleaveComponent &cleave) const;
    float CurrentReadyDuration(const CleaveComponent &cleave) const;

    // 準備中：全体スロー(cinematicSlow=globalScale経由で自機含む)を維持する。
    void HoldGlobalSlow(const CleaveComponent &cleave);
    // スローを即解除（突進の瞬間に等速へスナップ）。
    void ClearGlobalSlow();
    // 画面モノクロ演出の ON/OFF（ジャスト回避の演出singletonを hold で流用）。
    void SetMonochrome(bool on);
    // 照準方向へプレイヤーを向ける。
    void FaceAim(PlayerComponent &player, const DirectX::SimpleMath::Vector3 &dir);

    // このプレイヤーを映す TPS カメラへ「寄り(クローズアップ)」要求を push する。
    void PushCameraCloseUp(CCL::ECS::EntityID playerID, bool close);

    // 突進(Lunging)を開始。
    void StartLunge(CleaveComponent &cleave, const DirectX::SimpleMath::Vector3 &dir);

    // 突進終了：ヒットかつ残ストックがあれば再準備で連鎖、そうでなければ解除しクールダウンへ。
    void EndLunge(CleaveComponent &cleave, RootMotionComponent *rm,
                  const DirectX::SimpleMath::Vector3 &aim);

    // 待機(Idle)へ戻しクールダウンを張る（時間切れ・空振り時）。
    void ExitToIdle(CleaveComponent &cleave, RootMotionComponent *rm);

    // 前方の扇内のドローンを進行方向へ吹き飛ばす（ボスは対象外＝ドローンのみ走査）。
    void ApplyFanKnockback(const DirectX::SimpleMath::Vector3 &origin, CleaveComponent &cleave);
};
