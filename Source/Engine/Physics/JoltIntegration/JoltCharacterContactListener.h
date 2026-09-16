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
// - ここから EventBus へ即時 Publish してはいけない。物理更新の途中でゲームロジックが
//   割り込むと構造変更等が混ざって危険。剛体側と同じ JoltContactListener のバッファへ
//   PushEvent() で蓄積する。
//   実際の配送は物理更新後の L05 (JoltCollisionEventSystem) が一括で行う。
// - World には依存しない（データ競合の温床を断つ）。
// ===================================================================================

class JoltCharacterContactListener : public JPH::CharacterContactListener
{
private:
    JoltContactListener *m_sink; // 収集先バッファ（剛体側と共有）
    CCL::ECS::EntityID   m_playerID;
    JPH::BodyInterface  *m_bodyInterface;

public:
    JoltCharacterContactListener(JoltContactListener *sink,
                                 CCL::ECS::EntityID   playerID,
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