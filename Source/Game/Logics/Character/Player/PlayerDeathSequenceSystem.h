#pragma once
#include "ECS/System/CCL_System.h"
#include "Game/Logics/Character/Player/PlayerComponent.h"
#include "Game/Logics/Combat/DeadTag.h"

// ===================================================================================
// クラス名: PlayerDeathSequenceSystem
// 責務: Tag::DeadTagを持ったプレイヤーの死亡演出（シェイク、カメラ、フォグ、UIトリガー）の進行
// ===================================================================================
class PlayerDeathSequenceSystem
    : public CCL::ECS::IfSystem<PlayerDeathSequenceSystem, 
    CCL::ECS::Read<PlayerComponent>, 
    CCL::ECS::Read<Tag::DeadTag>> {
  public:
    PlayerDeathSequenceSystem();
    virtual ~PlayerDeathSequenceSystem() = default;

    void Update(float dt) override;

  private:
    // --- 旧DamageSystemから分離した演出ロジック ---

    // 画面全体を揺らす
    void ApplyBigShake(float strength, float duration);

    // プレイヤー死亡アニメーションとカメラの切り替え
    void TriggerPlayerDeathEffect(CCL::ECS::EntityID playerID);

    // 画面のフォグを徐々に濃くする処理
    void UpdateDeathFog(float dt, float totalTime, float currentTime);
};