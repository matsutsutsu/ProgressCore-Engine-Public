#include "Engine/Physics/JoltIntegration/JoltShapeFactory.h"
#include "Engine/Platform/Core/Logger.h"

#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>

namespace JoltShapeFactory
{

    JPH::ShapeRefC CreateChecked(
        const JPH::ShapeSettings &settings, uint64_t ownerId, const char *what)
    {
        // Create() は結果を mCachedResult にキャッシュするため、複数回呼んでも安い
        JPH::ShapeSettings::ShapeResult result = settings.Create();

        if (result.HasError())
        {
            // ERROR は全構成で残る（Logger.h）
            // 何も出さずに nullptr を返すと「一部のオブジェクトだけ当たり判定がない」という
            // 原因を追いにくい症状になるため、必ず理由ごと残す
            CCL_LOG_ERROR(LogCategory::Physics,
                          "%s の形状生成に失敗しました (Entity %llu): %s",
                          what,
                          static_cast<unsigned long long>(ownerId),
                          result.GetError().c_str());
            return nullptr;
        }

        return result.Get();
    }



    JPH::Quat EulerDegreeToJoltQuat(const DirectX::XMFLOAT3 &eulerDegree)
    {
        const float pitch = DirectX::XMConvertToRadians(eulerDegree.x);
        const float yaw   = DirectX::XMConvertToRadians(eulerDegree.y);
        const float roll  = DirectX::XMConvertToRadians(eulerDegree.z);
        return JPH::Quat::sEulerAngles(JPH::Vec3(pitch, yaw, roll));
    }



    JPH::ShapeRefC CreateOffsetShape(
        const JPH::ShapeSettings &base, const DirectX::XMFLOAT3 &localOffset,
        const DirectX::XMFLOAT3 &localEuler, uint64_t ownerId, const char *what)
    {
        JPH::ShapeRefC baseShape = CreateChecked(base, ownerId, what);
        if (baseShape == nullptr) return nullptr;

        JPH::RotatedTranslatedShapeSettings offsetSettings(
            JPH::Vec3(localOffset.x, localOffset.y, localOffset.z),
            EulerDegreeToJoltQuat(localEuler),
            baseShape);

        return CreateChecked(offsetSettings, ownerId, what);
    }

} // namespace JoltShapeFactory
