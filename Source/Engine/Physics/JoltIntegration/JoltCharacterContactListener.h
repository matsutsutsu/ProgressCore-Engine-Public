#pragma once
// ===========================================================================
// 概要: CharacterVirtual 専用の接触リスナー。接触をイベントにして共有バッファへ積む。
//
// 責務:
//   - 接触の追加・継続を JoltCollisionEvent にして JoltContactListener のバッファへ積む
//   - 背景（壁・床・見えない壁）との接触は捨てる
//   - 攻撃レイヤーとの接触だけ計測ログ（HitDiagnostics）を出す
//
// 備考: JoltCharacterUpdateSystem が ExtendedUpdate の間だけキャラクターに付ける。
//       ここから EventBus へ直接 Publish しない。物理更新の途中でゲームロジックが
//       割り込むと構造変更などが混ざるため、剛体側と同じバッファへ積み、
//       物理更新後の L05 で JoltCollisionEventSystem がまとめて配送する。
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
    CCL::ECS::EntityID   m_selfID;        // このリスナーを付けたキャラクター自身の Entity
    JPH::BodyInterface  *m_bodyInterface; // 相手ボディのレイヤーや UserData を引くため（所有しない）

public:
    JoltCharacterContactListener(JoltContactListener *sink,
                                 CCL::ECS::EntityID   selfID,
                                 JPH::BodyInterface  *bodyInterface);

    virtual void OnContactAdded(const JPH::CharacterVirtual   *inCharacter,
                                const JPH::BodyID             &inBodyID2,
                                const JPH::SubShapeID         &inSubShapeID2,
                                JPH::RVec3Arg                  inContactPosition,
                                JPH::Vec3Arg                   inContactNormal,
                                JPH::CharacterContactSettings &ioSettings) override;

    // 接触継続フレームに呼ばれる。Added と全く同じ扱いにする。
    //
    // CharacterVirtual は前フレームの接触を mListenerContacts に持ち越し、
    // 既知の対を Added ではなく Persisted に回す(CharacterVirtual.cpp)。
    // ここを実装しないと重なり続けている間の接触が全て消え、攻撃判定の窓より
    // 先に武器が相手へ入っているケース（突進攻撃など）が丸ごと不発になる。
    virtual void OnContactPersisted(const JPH::CharacterVirtual   *inCharacter,
                                    const JPH::BodyID             &inBodyID2,
                                    const JPH::SubShapeID         &inSubShapeID2,
                                    JPH::RVec3Arg                  inContactPosition,
                                    JPH::Vec3Arg                   inContactNormal,
                                    JPH::CharacterContactSettings &ioSettings) override;

    // 接触が解消された時に呼ばれる。
    //
    // 計測ログのみ。接触が正しく解放されているかの確認用で、判定には使わない。
    virtual void OnContactRemoved(const JPH::CharacterVirtual *inCharacter,
                                  const JPH::BodyID           &inBodyID2,
                                  const JPH::SubShapeID       &inSubShapeID2) override;

private:
    /**
     * @brief Added / Persisted 共通の実体。接触1件をイベント化して sink へ積む。
     * @param inPhase 計測ログ用の表示ラベル("Add" / "Persist")。判定には影響しない。
     */
    void EmitContactEvent(const JPH::CharacterVirtual *inCharacter,
                          const JPH::BodyID           &inBodyID2,
                          JPH::RVec3Arg                inContactPosition,
                          JPH::Vec3Arg                 inContactNormal,
                          const char                  *inPhase);

    // 攻撃レイヤー(PLAYER_ATTACK / BOSS_ATTACK)との接触かを判定する。
    //
    // 地面・壁・相手本体まで出すとログが埋まるため、計測は攻撃判定だけに絞る。
    bool IsAttackLayer(const JPH::BodyID &bodyID) const;
};