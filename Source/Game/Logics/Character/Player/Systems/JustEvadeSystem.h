#pragma once
// ===========================================================================
// 概要: 回避ダッシュ中の接触からジャスト回避の成立を判定し、成立時の処理を行う System。
//
// 責務:
//   - JoltCollisionEvent を溜め、Update でジャスト回避の成立を判定する
//   - 成立したら JustEvadeSuccessEvent と、カメラの画角を広げる要求を発行する
//   - 成立時にヒットストップと世界スロー、接近の開始、専用モーション、画面演出を始める
//   - 成立フラグと、同じ相手に続けて成立させないための記録を片付ける
//
// 備考: 成立するのは、突撃中のドローンか、判定が開いている Hitbox に受付時間内に触れたときだけ。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Player/Components/JustEvadeComponent.h"
#include "Game/Logics/Character/Player/Components/DashComponent.h"
#include "Engine/Time/TimeState.h"
#include "Engine/Physics/Components/Collision/JoltCollisionEvent.h"

#include <vector>

struct DroneComponent; // ポインタ参照のみのため前方宣言で足りる

// TimeStateComponent が Write なのは、成立時に Time::HitStopEntity が
// hitStopTimer / freezeScale を書き換えるため（読み取りだけではない）。
class JustEvadeSystem : public CCL::ECS::IfSystem<JustEvadeSystem,
                                                  CCL::ECS::Write<JustEvadeComponent>,
                                                  CCL::ECS::Write<DashComponent>,
                                                  CCL::ECS::Write<TimeStateComponent>>
{
public:
    JustEvadeSystem()
        : IfSystem("JustEvadeSystem")
    {
    }

    void Initialize() override;
    void Update(float dt) override;

    // 成立時のイベントハンドラが書く型を、テンプレート引数の分に足して申告する。
    //
    // テンプレート引数に足すと ForEach の Archetype フィルタまで狭まり、
    // ループの対象から外れてしまう。フィルタと申告は別なのでここで足す。
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override;

    // 接触の配送が終わってから走らせる（同じ L05_Collision のため順序が不定になる）。
    std::vector<const char *> GetRunAfter() const override
    {
        return {"JoltCollisionEventSystem"};
    }

private:
    void CheckJustEvade(CCL::ECS::EntityID playerCandidate, CCL::ECS::EntityID otherCandidate);

    // このフレームに届いた接触。コールバックが溜め、Update が処理して空にする。
    //
    // EventBus は同期配送なので、コールバック内で判定すると処理が発行元
    // (JoltCollisionEventSystem)のタスク上で走り、この System の申告が
    // スケジューラから見えなくなる。溜めるだけにして、判定は Update で行う。
    std::vector<JoltCollisionEvent> m_pendingContacts;

    // そのドローンが、今ぶつかったらジャスト回避が成立する相手か（突撃中で、吹き飛ばされていない）。
    // 隊列で待機しているだけのドローンにぶつかっても成立しないようにする。
    bool IsDroneHostile(const DroneComponent *drone) const;
};