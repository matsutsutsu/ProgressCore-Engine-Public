#pragma once
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include "Engine/ECS/Core/CCL_World.h"

// ===================================================================================
// ファイル: JoltCharacterContactListener.h
// 役割: 仮想キャラクター (CharacterVirtual) 専用の物理衝突検知リスナー。
//
// 【アーキテクチャ仕様】
// - CharacterVirtual の ExtendedUpdate 実行中に一時的にアタッチされる。
// - 通常の剛体用リスナー(JoltContactListener)はマルチスレッドから呼ばれるが、
//   本クラスは ECS の ForEachWithID 内（シングルスレッド）で同期的に呼ばれるため、
//   ミューテックス(mutex)等のロックを必要とせず、直接 EventBus へ送信できる設計となっている。
// ===================================================================================

class JoltCharacterContactListener : public JPH::CharacterContactListener
{
  private:
    CCL::ECS::Core::World *m_world;
    CCL::ECS::EntityID     m_playerID;
    JPH::BodyInterface    *m_bodyInterface;

  public:
    JoltCharacterContactListener(CCL::ECS::Core::World *world, CCL::ECS::EntityID playerID, JPH::BodyInterface *bodyInterface);

    virtual void OnContactAdded(const JPH::CharacterVirtual *inCharacter,
        const JPH::BodyID                                   &inBodyID2,
        const JPH::SubShapeID                               &inSubShapeID2,
        JPH::RVec3Arg                                        inContactPosition,
        JPH::Vec3Arg                                         inContactNormal,
        JPH::CharacterContactSettings                       &ioSettings) override;
};