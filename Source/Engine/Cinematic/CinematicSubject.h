#pragma once
// ムービーの対象（subject）を Entity の名前で探し、今の位置と向きを求める関数
// カメラ・トラック・エディタが同じ探し方をするので1か所に置く。全 Entity を比べるので、再生中は区切りの瞬間にだけ呼ぶ
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Core/Components/NameComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"

#include <SimpleMath.h>
#include <cmath>
#include <string>

namespace Cinematic
{
    // 名前（NameComponent）で Entity を探す。最初に見つかったものを返し、なければ InvalidEntityID
    inline CCL::ECS::EntityID FindEntityByName(CCL::ECS::World &world, const std::string &name)
    {
        CCL::ECS::EntityID found = CCL::ECS::InvalidEntityID;

        world.ViewEach<NameComponent>([&](CCL::ECS::EntityID id) {
            // すでに見つかっていたら、残りの Entity は無視する
            if (found != CCL::ECS::InvalidEntityID) return;

            // 名前が一致する Entity を探す
            const auto *nameComp = world.GetComponent<NameComponent>(id);
            if (nameComp && name == nameComp->name) found = id;
        });
        return found;
    }

    // 水平の向き（ラジアン、0 が +Z）。求まらなければ 0
    inline float YawOf(const TransformComponent &trans)
    {
        // 描画行列の Z 軸（3行目）が前方
        // 水平成分だけで向きを決める（傾きや見上げ角は構図に入れない）
        const auto &m  = trans.renderWorldMatrix;
        const float fx = m._31;
        const float fz = m._33;
        return (fx * fx + fz * fz > 1e-8f) ? std::atan2(fx, fz) : 0.0f;
    }

    // 名前で対象を探し、描画用に補間した位置と向き（ラジアン、0 が +Z）を返す。見つからなければ false
    // 名前が空なら、ワールドの原点（向き 0）
    inline bool FindSubjectPose(CCL::ECS::World &world, const std::string &name, DirectX::SimpleMath::Vector3 &outPos,
                                float &outYaw)
    {
        outPos = DirectX::SimpleMath::Vector3::Zero;
        outYaw = 0.0f;
        if (name.empty()) return true;

        const CCL::ECS::EntityID subject = FindEntityByName(world, name);
        if (subject == CCL::ECS::InvalidEntityID) return false;

        const auto *trans = world.GetComponent<TransformComponent>(subject);
        if (!trans) return false;

        outPos = trans->GetRenderWorldPosition();
        outYaw = YawOf(*trans);
        return true;
    }
} // namespace Cinematic
