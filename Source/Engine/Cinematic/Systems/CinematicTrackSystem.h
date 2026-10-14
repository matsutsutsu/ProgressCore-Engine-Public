#pragma once
// ===========================================================================
// 概要: 再生中のムービーの台本から、カメラ以外のトラック（SE・エフェクト・シグナル・アニメ）を出す System。
//
// 責務:
//   - 再生位置が時刻を過ぎた点（SE・エフェクト・シグナル）を1回ずつ出す。スキップで飛ばした分は出さない
//   - アニメの区間に入ったら対象のアニメを握り、区間を出たら・ムービーが終わったら離す
//   - 新しい再生に入ったら、前の再生の続き（握ったままのアニメ）を片付けて頭から追う
//
// 備考:
//   再生位置を進めるのは CinematicCameraSystem（描画の段）。この System はロジックの段で、それを読むだけ。
//   ロジックの段に置くのは、SE とエフェクトの受け手（AudioSystem・VFXSystem）とアニメの持ち主
//   （AnimControllerSystem）がロジックの段にあるため。
//   再生位置を読むのは次のロジックの更新なので、点は固定ステップ1回分まで遅れて出る。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Engine/Cinematic/Components/CinematicCameraComponent.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Core/Components/NameComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"

#include <functional>
#include <vector>

class CinematicTrackSystem : public CCL::ECS::IfSystem<CinematicTrackSystem,
                                                       CCL::ECS::Write<CinematicCameraComponent>>
{
public:
    CinematicTrackSystem() : IfSystem("CinematicTrackSystem"){}

    void Update(float dt) override;

    /**
     * @brief 書き込む Component 型をスケジューラへ申告する。
     *
     * 対象の AnimatorComponent を GetComponent で書く（アニメを握る・離す）。
     * ForEach のフィルタには載らないので、ここで足しておかないと、それを書く System と並列に走ってしまう。
     */
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override
    {
        auto types = IfSystem::GetWriteTypes();
        types.push_back(CCL::ECS::TypeInfo<AnimatorComponent>::ID());
        return types;
    }

    /**
     * @brief 読み取る Component 型をスケジューラへ申告する。
     *
     * 対象の名前（NameComponent）で Entity を探し、SE・エフェクトの受け手がその位置（TransformComponent）を読む。
     */
    std::vector<CCL::ECS::TypeID> GetReadTypes() const override
    {
        auto types = IfSystem::GetReadTypes();
        types.push_back(CCL::ECS::TypeInfo<NameComponent>::ID());
        types.push_back(CCL::ECS::TypeInfo<TransformComponent>::ID());
        return types;
    }

private:
    using FireList = std::vector<std::function<void()>>; // ForEach を抜けてから出す知らせ

    /**
     * @brief 再生位置を過ぎた点（SE・エフェクト・シグナル）を集め、次に出す番号を進める。
     * @param cine 再生状態。スキップで終えていたら、集めずに番号だけ最後まで進める。
     * @param out  出す知らせを足す先。
     */
    void CollectPoints(CinematicCameraComponent &cine, FireList &out) const;

    /**
     * @brief アニメの区間を出たものを離し、入ったものを握る。
     * @param cine 再生状態。再生中でなければ、握っているものをすべて離す。
     */
    void SyncAnimations(CinematicCameraComponent &cine) const;

    // 対象の AnimatorComponent を握り、クリップを頭から再生させる。握れたら true
    bool HoldAnimation(CCL::ECS::EntityID entity, const CinematicAnimation &clip) const;

    // 対象の AnimatorComponent を離し、元のステートへ blendOut 秒で戻させる
    void ReleaseAnimation(CCL::ECS::EntityID entity, float blendOut) const;
};
