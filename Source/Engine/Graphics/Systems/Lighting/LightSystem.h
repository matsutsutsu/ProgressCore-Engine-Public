#pragma once
// ===========================================================================
// 概要: ライトのコンポーネントを集めて LightManager に積む System（R02_Environment）。
//
// 責務:
//   - 毎フレーム LightManager を空にし、平行光源・点光源・スポットライトを積み直す
//   - 位置と向きを TransformComponent から求める
//
// 備考: GPU への転送（CbLight）は Graphics::Update が毎フレーム行う。
//       平行光源は1つだけ持てるので、複数あると最後に見つかったものが使われる。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include <DirectXMath.h>
#include <vector>

class LightSystem : public CCL::ECS::SystemBase
{
public:
    LightSystem() : SystemBase("LightSystem") {}
    virtual ~LightSystem() = default;

    std::vector<CCL::ECS::TypeID> GetReadTypes() const override;
    void                          Update(float dt) override;

private:
    // 回転クォータニオンで +Z を回した向き（ライトの向き）を返す
    DirectX::XMFLOAT3 CalculateDirection(const DirectX::XMFLOAT4 &rotation);
};
