#pragma once
// ===========================================================================
// 概要: 再生中のムービーの台本から、カメラ以外のトラック（SE・エフェクト・シグナル・アニメ）を出す System
//
// 責務:
//   - 再生位置が時刻を過ぎた点（SE・エフェクト・シグナル）を1回ずつ出す。スキップで飛ばした分は出さない
//   - アニメの区間に入ったら対象のアニメを握り、区間を出たら・ムービーが終わったら離す
//   - 新しい再生に入ったら、前の再生の続き（握ったままのアニメ）を片付けて頭から追う
//   - 途中から流す再生では、開始位置より前の点を出さず、区間の途中のアニメはその分進めた位置から握る
//
// 備考: 再生位置を進めるのは CinematicCameraSystem（描画の段）で、この System はそれを読むだけ
//       受け手（AudioSystem・VFXSystem・AnimControllerSystem）がロジックの段にあるので、ここもロジックに置く
//       そのため点は、固定ステップ1回分まで遅れて出る
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
    CinematicTrackSystem() : IfSystem("CinematicTrackSystem"){}

    void Update(float dt) override;

    // 対象の AnimatorComponent を GetComponent で書く（アニメを握る・離す）ので申告する
    // 申告しないと、それを書く System と並列に走ってしまう
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override
    {
        auto types = IfSystem::GetWriteTypes();
        types.push_back(CCL::ECS::TypeInfo<AnimatorComponent>::ID());
        return types;
    }

    // 対象を名前（NameComponent）で探し、SE・エフェクトの受け手がその位置（TransformComponent）を読む
    std::vector<CCL::ECS::TypeID> GetReadTypes() const override
    {
        auto types = IfSystem::GetReadTypes();
        types.push_back(CCL::ECS::TypeInfo<NameComponent>::ID());
        types.push_back(CCL::ECS::TypeInfo<TransformComponent>::ID());
        return types;
    }

private:
    using FireList = std::vector<std::function<void()>>; // ForEach を抜けてから出す知らせ

    // 再生位置を過ぎた点（SE・エフェクト・シグナル）を out へ集め、次に出す番号を進める
    // スキップで終えていたら、集めずに番号だけ最後まで進める
    void CollectPoints(CinematicCameraComponent &cine, FireList &out) const;

    // アニメの区間を出たものを離し、入ったものを握る。再生中でなければ、握っているものをすべて離す
    void SyncAnimations(CinematicCameraComponent &cine) const;

    // 対象の AnimatorComponent を握り、クリップを再生させる。握れたら true
    // clipOffset はクリップ内の再生位置（秒）で、途中から握るときだけ 0 より大きい
    bool HoldAnimation(CCL::ECS::EntityID entity, const CinematicAnimation &clip, float clipOffset) const;

    // 対象の AnimatorComponent を離し、元のステートへ blendOut 秒で戻させる
    void ReleaseAnimation(CCL::ECS::EntityID entity, float blendOut) const;
};
