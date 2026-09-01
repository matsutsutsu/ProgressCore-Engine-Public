#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Player/Components/JustEvadeComponent.h"
#include "Game/Logics/Character/Player/Components/DashComponent.h"
#include "Engine/Time/TimeState.h"
#include "Engine/Physics/Components/Collision/JoltCollisionEvent.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"

#include <vector>

struct DroneComponent; // ポインタ参照のみのため前方宣言で足りる

// TimeStateComponent が Write なのは、成立時に Time::HitStopEntity が
// hitStopTimer / freezeScale を書き換えるため（読み取りだけではない）。
class JustEvadeSystem : public CCL::ECS::IfSystem<JustEvadeSystem,
                            CCL::ECS::Write<JustEvadeComponent>,
                            CCL::ECS::Write<DashComponent>,
                            CCL::ECS::Write<CharacterMovementComponent>,
                            CCL::ECS::Write<TimeStateComponent>>
{
  public:
    JustEvadeSystem()
        : IfSystem("JustEvadeSystem")
    {
        hasGui = true; // チェインHUDを OnGui で前面描画する
    }

    void Initialize() override;
    void Update(float dt) override;
    void OnGui() override;

    // 成立時のイベントハンドラが書く分をテンプレート宣言へ足す。
    //
    // テンプレート引数に足すと ForEach の Archetype フィルタまで狭まり、
    // ループ対象から外れてしまう。フィルタと申告は別物なのでここで足す。
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override;

    // 接触の配送が終わってから走らせる（同じ L05_Collision のため順序が不定になる）。
    std::vector<const char *> GetRunAfter() const override
    {
        return {"JoltCollisionEventSystem"};
    }

  private:
    void CheckJustEvade(CCL::ECS::EntityID playerCandidate, CCL::ECS::EntityID otherCandidate);
    void BreakChain(CCL::ECS::EntityID playerID); // チェインを初期状態へ戻す

    // このフレームに届いた接触。コールバックが積み、Update が消費して空にする。
    //
    // EventBus は同期配送なので、コールバック内で判定すると処理が発行元
    // (JoltCollisionEventSystem)のタスク上で走り、本システムの申告が
    // スケジューラから見えなくなる。積むだけに留めて判定は Update で行う。
    std::vector<JoltCollisionEvent> m_pendingContacts;

    // そのドローンが「今ぶつかったらジャスト回避が成立する相手」かを判定する門番。
    // 陣形を組んでいるだけのドローンに突っ込んでも回避が暴発しないようにする。
    bool IsDroneHostile(const DroneComponent *drone) const;
};