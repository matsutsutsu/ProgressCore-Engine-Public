#include "Engine/Physics/JoltIntegration/JoltShapeFactory.h"
#include "Engine/Platform/Core/Logger.h"

namespace JoltShapeFactory {

JPH::ShapeRefC CreateChecked(
    const JPH::ShapeSettings &settings, uint64_t ownerId, const char *what)
{
    // Create() は結果を mCachedResult にキャッシュするため、複数回呼んでも安い。
    JPH::ShapeSettings::ShapeResult result = settings.Create();

    if (result.HasError())
    {
        // ERROR は全構成で残る（Logger.h: 販売後のクラッシュ解析の生命線）。
        // 無言で nullptr を返すと「一部のオブジェクトだけ当たり判定が無い」という
        // 極めて追いにくい症状になるため、必ず理由ごと残す。
        CCL_LOG_ERROR(LogCategory::Physics,
            "%s の形状生成に失敗しました (Entity %llu): %s",
            what,
            static_cast<unsigned long long>(ownerId),
            result.GetError().c_str());
        return nullptr;
    }

    return result.Get();
}

} // namespace JoltShapeFactory
