#pragma once
// ===========================================================================
// 概要: CharacterVirtual 専用の接触リスナー。接触をイベントにして共有バッファへ積む。
//
// 責務:
//   - 接触の追加・継続を JoltCollisionEvent にして、JoltContactListener のバッファへ積む
//   - 背景（壁・床・見えない壁）との接触は捨てる
//   - 攻撃レイヤーとの接触だけ、計測ログ（HitDiagnostics）を出す
//
// 備考: JoltCharacterUpdateSystem が、ExtendedUpdate の間だけキャラクターに付ける。
//       EventBus へ直接出さず、剛体側と同じバッファへ積む。
//       物理更新の途中でゲームロジックを走らせないためで、配送は L05 の JoltCollisionEventSystem。
//       World には触れない（ワーカースレッドとのデータ競合を避ける）。
// ===========================================================================
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Body/BodyInterface.h>

#include "Engine/ECS/Common/CCL_Types.h"
#include "Engine/Physics/JoltIntegration/JoltContactListener.h" // 収集先バッファ(sink)

class JoltCharacterContactListener : public JPH::CharacterContactListener
{
private:
    JoltContactListener *m_sink;          // 収集先バッファ（剛体側と共有。所有しない）
    CCL::ECS::EntityID   m_selfID;        // このリスナーを付けたキャラクター自身
    JPH::BodyInterface  *m_bodyInterface; // 相手のレイヤーと UserData を引く（所有しない）

public:
    JoltCharacterContactListener(JoltContactListener *sink,
                                 CCL::ECS::EntityID   selfID,
                                 JPH::BodyInterface  *bodyInterface);

    // 接触が始まったときに呼ばれる。イベントにして積む
    virtual void OnContactAdded(const JPH::CharacterVirtual   *inCharacter,
                                const JPH::BodyID             &inBodyID2,
                                const JPH::SubShapeID         &inSubShapeID2,
                                JPH::RVec3Arg                  inContactPosition,
                                JPH::Vec3Arg                   inContactNormal,
                                JPH::CharacterContactSettings &ioSettings) override;

    // 接触が続いているフレームに呼ばれる。Added と同じくイベントにして積む
    // これが無いと、攻撃判定が出る前から重なっていた相手（突進攻撃など）に当たらない
    virtual void OnContactPersisted(const JPH::CharacterVirtual   *inCharacter,
                                    const JPH::BodyID             &inBodyID2,
                                    const JPH::SubShapeID         &inSubShapeID2,
                                    JPH::RVec3Arg                  inContactPosition,
                                    JPH::Vec3Arg                   inContactNormal,
                                    JPH::CharacterContactSettings &ioSettings) override;

    // 接触が終わったときに呼ばれる。計測ログだけで、判定には使わない
    virtual void OnContactRemoved(const JPH::CharacterVirtual *inCharacter,
                                  const JPH::BodyID           &inBodyID2,
                                  const JPH::SubShapeID       &inSubShapeID2) override;

private:
    /**
     * @brief Added / Persisted 共通の処理。接触1件をイベントにして sink へ積む。
     * @param inPhase 計測ログに出す名前（"Add" / "Persist"）。判定には影響しない。
     */
    void EmitContactEvent(const JPH::CharacterVirtual *inCharacter,
                          const JPH::BodyID           &inBodyID2,
                          JPH::RVec3Arg                inContactPosition,
                          JPH::Vec3Arg                 inContactNormal,
                          const char                  *inPhase);

    // 攻撃レイヤー（PLAYER_ATTACK / BOSS_ATTACK）との接触か
    // 計測ログを攻撃判定だけに絞るために使う
    bool IsAttackLayer(const JPH::BodyID &bodyID) const;
};
