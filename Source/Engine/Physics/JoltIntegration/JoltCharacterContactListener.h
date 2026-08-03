#pragma once
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Body/BodyInterface.h>

#include "Engine/ECS/Common/CCL_Types.h"
#include "Engine/Physics/JoltIntegration/JoltContactListener.h" // 収集先バッファ(sink)

// ===================================================================================
// ファイル: JoltCharacterContactListener.h
// 役割: 仮想キャラクター (CharacterVirtual) 専用の物理衝突検知リスナー。
//
// 【アーキテクチャ仕様】
// - CharacterVirtual の ExtendedUpdate 実行中に一時的にアタッチされる。
// - 以前は EventBus へ即時 Publish していたが、物理更新の途中でゲームロジックが
//   割り込むと（構造変更等が混ざり）危険なため、剛体側と同じ JoltContactListener の
//   バッファへ PushEvent() で蓄積する方式に統一した。
//   実際の配送は物理更新後の L05 (JoltCollisionEventSystem) が一括で行う。
// - World には依存しない（データ競合の温床を断つ）。
// ===================================================================================

class JoltCharacterContactListener : public JPH::CharacterContactListener
{
  private:
    JoltContactListener *m_sink; ///< 収集先バッファ（剛体側と共有）
    CCL::ECS::EntityID   m_playerID;
    JPH::BodyInterface  *m_bodyInterface;

  public:
    JoltCharacterContactListener(JoltContactListener *sink,
        CCL::ECS::EntityID                            playerID,
        JPH::BodyInterface                           *bodyInterface);

    virtual void OnContactAdded(const JPH::CharacterVirtual *inCharacter,
        const JPH::BodyID                                   &inBodyID2,
        const JPH::SubShapeID                               &inSubShapeID2,
        JPH::RVec3Arg                                        inContactPosition,
        JPH::Vec3Arg                                         inContactNormal,
        JPH::CharacterContactSettings                       &ioSettings) override;
};