#pragma once
// ===========================================================================
// 概要: アニメーションの SpawnHitbox イベントを受けて球の攻撃判定を作り、寿命が来たら消す System
//
// 責務:
//   - 追従しない判定は、作った瞬間のワールド位置に固定する
//   - 追従する判定は持ち主の子にし、持ち主のアニメが切り替わったら寿命の途中でも消す
//   - Transform・Hitbox・センサーの剛体・球のコライダ・寿命を持つエンティティを作る
//   - 寿命を減らし、尽きたら消す
//
// 備考: 剛体の作成は JoltBodySetupSystem、命中は HitboxCollisionSystem、片付けは JoltCleanupSystem に任せる
//       区間で開け閉めする剣（CombatAnimationSyncSystem）とは別の方法で、作った判定は寿命まで開いたまま
//       書くのは HitboxLifetimeComponent だけなので、HitboxComponent を書く CombatAnimationSyncSystem と競合しない
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Combat/Components/CombatComponents.h"

struct AnimEventSpawnHitboxMessage;

class SpawnHitboxSystem : public CCL::ECS::IfSystem<
    SpawnHitboxSystem,
    CCL::ECS::Write<HitboxLifetimeComponent>>
{
public:
    SpawnHitboxSystem() : IfSystem("SpawnHitboxSystem") {}

    void Initialize() override;
    void Update(float dt) override;

private:
    // イベント1つから、球の攻撃判定のエンティティを1つ作る。持ち主に Transform がなければ作らない
    void OnSpawnHitbox(const AnimEventSpawnHitboxMessage &msg);
};
