#pragma once
#include "Engine/ECS/Common/CCL_Types.h"

// 親子付けの予約。生成直後でまだ SetParent できない Entity に付ける
// TransformUpdateSystem が次の更新で SetParent して外す。parentID が無効なら親から切り離す
struct PendingParentComponent
{
    CCL::ECS::EntityID parentID = CCL::ECS::InvalidEntityID; // 付けたい親（0 / InvalidEntityID なら切り離す）
};
