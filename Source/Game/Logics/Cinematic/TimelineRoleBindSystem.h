#pragma once
// ===========================================================================
// 概要: タイムラインの役割名（"Player" / "Boss"）を、実際のエンティティへ結び付ける System。
//
// 責務:
//   - BattleContext のプレイヤー/ボスを、全 TimelinePlayer の bindings へ書き込む
//
// 備考:
//   なぜゲーム側にあるか:
//   どのエンティティがプレイヤーかはゲーム側の知識で、Engine のタイムラインが
//   PlayerComponent を知ると Engine→Game の依存になる。CameraTargetBindSystem と同じ考え方。
//
//   役割を増やすとき（ドローン群の代表など）はここに1行足す。資産側は役割名で書くだけでよい。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Timeline/Components/TimelinePlayerComponent.h"

#include <vector>

class TimelineRoleBindSystem : public CCL::ECS::IfSystem<
                                   TimelineRoleBindSystem,
                                   CCL::ECS::Write<TimelinePlayerComponent>>
{
public:
    TimelineRoleBindSystem()
        : IfSystem("TimelineRoleBindSystem")
    {
    }
    virtual ~TimelineRoleBindSystem() = default;

    void Update(float dt) override;

    // 再生の開始判定（TimelineSystem）より先に結び付けておく。同じ L01_Input なので名前で縛る
    std::vector<const char *> GetRunBefore() const override { return {"TimelineSystem"}; }
};
