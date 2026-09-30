#pragma once
// ===========================================================================
// 概要: アニメーションの SpawnHitbox イベントを受けて、球の攻撃判定を作り、
//       寿命が来たら消す System。
//
// 責務:
//   - 追従しない判定: offset を持ち主の回転で回し、作った瞬間のワールド位置に固定する
//   - 追従する判定: 持ち主の子にして offset をローカル座標とし、持ち主に付いていかせる
//   - Transform・Hitbox・センサーの剛体・球のコライダ・寿命を持つエンティティを作る
//     （当てる相手はイベントの attackLayer で決める: PLAYER_ATTACK / BOSS_ATTACK）
//   - 寿命（HitboxLifetimeComponent）を減らし、尽きたら消す。
//     追従する判定は、持ち主のアニメーションが切り替わったら寿命の途中でも消す（HitboxLifeAnimComponent）
//
// 備考: 作った後は他の System に任せる。JoltBodySetupSystem（L03）がセンサーの剛体を作り、
//       HitboxCollisionSystem（L05）が命中を判定し、消したら JoltCleanupSystem が剛体を片付ける。
//       剣のように子のコライダを区間で開け閉めする CombatAnimationSyncSystem とは別の方法。
//       作るときに ownerResolved を true にするので、CombatAnimationSyncSystem は触らず、寿命まで開いたままになる。
//       Write するのは HitboxLifetimeComponent だけなので、HitboxComponent を書く
//       CombatAnimationSyncSystem と競合しない。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Combat/CombatComponents.h"

struct AnimEventSpawnHitboxMessage;

class SpawnHitboxSystem
    : public CCL::ECS::IfSystem<SpawnHitboxSystem,
                                CCL::ECS::Write<HitboxLifetimeComponent>>
{
public:
    SpawnHitboxSystem() : IfSystem("SpawnHitboxSystem") {}

    void Initialize() override;
    void Update(float dt) override;

private:
    /**
     * @brief イベント1つから、球の攻撃判定のエンティティを1つ作る。
     * @param msg 作るときの値（持ち主・位置・半径・ダメージ・ヒットストップ・寿命）
     * @note  持ち主に Transform が無いと位置が決まらないので作らない。
     */
    void OnSpawnHitbox(const AnimEventSpawnHitboxMessage &msg);
};
