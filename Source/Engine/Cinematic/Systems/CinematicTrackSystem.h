#pragma once
// ===========================================================================
// 概要: 再生中のムービーの台本から、カメラ以外のトラック（SE・エフェクト・シグナル・アニメ）を出す System
//
// 責務:
//   - 再生位置が時刻を過ぎた点（SE・エフェクト・シグナル）を1回ずつ出す。スキップで飛ばした分は出さない
//   - アニメの区間に入ったら対象のアニメを握り、区間を出たら離す
//
// 備考: 再生位置を進めるのは CinematicCameraSystem（描画の段）。受け手がロジックの段にあるので、ここもロジックに置く
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Engine/Cinematic/Components/CinematicCameraComponent.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Core/Components/NameComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"

#include <functional>
#include <vector>

class CinematicTrackSystem : public CCL::ECS::IfSystem<
    CinematicTrackSystem,
    CCL::ECS::Write<CinematicCameraComponent>>
{
public:
    CinematicTrackSystem() : IfSystem("CinematicTrackSystem") {}

    void Update(float dt) override;

    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override
    {
        auto types = IfSystem::GetWriteTypes();
        types.push_back(CCL::ECS::TypeInfo<AnimatorComponent>::ID()); // 対象のアニメを握る・離す
        return types;
    }

    std::vector<CCL::ECS::TypeID> GetReadTypes() const override
    {
        auto types = IfSystem::GetReadTypes();
        types.push_back(CCL::ECS::TypeInfo<NameComponent>::ID());      // 対象を名前で探す
        types.push_back(CCL::ECS::TypeInfo<TransformComponent>::ID()); // SE・エフェクトの受け手が位置を読む
        return types;
    }

private:
    using FireList = std::vector<std::function<void()>>; // ForEach を抜けてから出す知らせ

    // 新しい再生に入ったら、握ったままのアニメを離し、開始位置より前の点を飛ばして追い直す
    void Restart(CinematicCameraComponent &cine) const;

    // 再生位置を過ぎた点を out へ集め、次に出す番号を進める
    void CollectPoints(CinematicCameraComponent &cine, FireList &out) const;

    // アニメの区間を出たものを離し、入ったものを握る
    void SyncAnimations(CinematicCameraComponent &cine) const;

    // 対象のアニメを握り、クリップを再生させる。clipOffset はクリップ内の開始位置（秒）
    bool HoldAnimation(CCL::ECS::EntityID entity, const CinematicAnimation &clip, float clipOffset) const;

    // 対象のアニメを離し、元のステートへ blendOut 秒で戻させる
    void ReleaseAnimation(CCL::ECS::EntityID entity, float blendOut) const;
};
