#pragma once
// ===========================================================================
// 概要: ムービーの対象（subject）を、Entity の名前で探す関数。
//
// 責務:
//   - 名前（NameComponent）が一致する最初の Entity を返す
//   - 対象の水平の向きを求める
//   - 対象の今の位置と向きを、名前から求める
//
// 備考:
//   カメラ（CinematicCameraSystem）とトラック（CinematicTrackSystem）とエディタが同じ探し方をするので、1か所に置く。
//   全 Entity の名前を比べるので、再生中は毎フレームではなく、ショットに入る・点を出す・区間に入る瞬間にだけ呼ぶ。
//   エディタのプレビューだけは時刻が前後に飛ぶので毎フレーム呼ぶ（再生していない間だけなので許す）。
// ===========================================================================
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Core/Components/NameComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"

#include <SimpleMath.h>
#include <cmath>
#include <string>

namespace Cinematic
{
    /**
     * @brief 名前（NameComponent）から Entity を探す。
     * @param world 探す World。
     * @param name  探す名前。
     * @return 最初に見つかった Entity。見つからなければ InvalidEntityID。
     */
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

    /**
     * @brief 対象の水平の向きを求める。
     * @param trans 対象の Transform。
     * @return 向き（ラジアン、0 が +Z）。向きが求まらなければ 0。
     */
    inline float YawOf(const TransformComponent &trans)
    {
        // 描画行列の Z 軸（3行目）が前方。
        // 水平成分だけで向きを決める（傾きや見上げ角は構図に入れない）。
        const auto &m  = trans.renderWorldMatrix;
        const float fx = m._31;
        const float fz = m._33;
        return (fx * fx + fz * fz > 1e-8f) ? std::atan2(fx, fz) : 0.0f;
    }

    /**
     * @brief 対象の今の位置と向きを、名前で探して求める。
     * @param world  探す World。
     * @param name   対象の名前。空ならワールドの原点（向き 0）。
     * @param outPos 対象の位置（ワールド、m）。描画用に補間した位置。
     * @param outYaw 対象の向き（ラジアン、0 が +Z）。
     * @return 名前の対象が見つからなければ false。
     */
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
