#pragma once
// ===========================================================================
// 概要: 剛体の接触リスナー兼、接触イベントのスレッドセーフな蓄積バッファ
//
// 責務:
//   - 剛体どうしの接触の追加・継続を JoltCollisionEvent にしてバッファへ積む
//   - キャラクター側（JoltCharacterContactListener）からの PushEvent も受け付ける
//   - 溜めたイベントを GetAndClearEvents でまとめて渡す
//
// 備考: コールバックは Jolt のワーカースレッドから並列に呼ばれるので、バッファは
//       mutex で守る。World やコンポーネントには触れない（データ競合になる）
//       配送は物理更新後の L05 で JoltCollisionEventSystem が行う
// ===========================================================================
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <mutex>
#include <vector>
#include "Engine/Physics/Components/Collision/JoltCollisionEvent.h"

class JoltContactListener : public JPH::ContactListener
{
public:
    // 接触が始まった瞬間に、Jolt のワーカースレッドから並列に呼ばれる
    virtual void OnContactAdded(const JPH::Body &inBody1, const JPH::Body &inBody2,
                                const JPH::ContactManifold &inManifold,
                                JPH::ContactSettings       &ioSettings) override;

    // 接触が続いている間に呼ばれる（2フレーム目以降）。Added と同じに扱う
    // Jolt は同じ対の2回目以降を Persisted に回すので、これがないと、判定の区間より先に武器が入っていた攻撃が当たらない
    virtual void OnContactPersisted(const JPH::Body &inBody1, const JPH::Body &inBody2,
                                    const JPH::ContactManifold &inManifold,
                                    JPH::ContactSettings       &ioSettings) override;

    // 接触が解けたときに呼ばれる。確認用の計測ログだけで、判定には使わない
    // この中では Body に触れない（Jolt の仕様）ので、EntityID でなく BodyID の index を記録する
    virtual void OnContactRemoved(const JPH::SubShapeIDPair &inSubShapePair) override;

    // 衝突イベントを積む。どのスレッドから呼んでもよい。剛体とキャラクターのイベントをこの1つに集める
    void PushEvent(const JoltCollisionEvent &ev);

    // 積んだイベントをすべて取り出して空にする。呼ぶのは物理の後の JoltCollisionEventSystem だけ（1ステップに1回）
    std::vector<JoltCollisionEvent> GetAndClearEvents();

private:
    // Added / Persisted の共通の処理。inPhase は計測ログの表示用（"Add" / "Persist"）
    void EmitContactEvent(const JPH::Body &inBody1, const JPH::Body &inBody2,
                          const JPH::ContactManifold &inManifold, const char *inPhase);

    std::mutex                      m_mutex;  // スレッド間競合を防ぐための排他ロック
    std::vector<JoltCollisionEvent> m_events; // 蓄積される衝突イベントのバッファ
};