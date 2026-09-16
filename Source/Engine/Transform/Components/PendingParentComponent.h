#pragma once
#include "Engine/ECS/Common/CCL_Types.h"

// 生成直後のエンティティなど、まだSetParentできない場合に
// 「後でこの親を設定してね」と予約するためのコンポーネント
struct PendingParentComponent
{
    CCL::ECS::EntityID parentID = CCL::ECS::InvalidEntityID;
};
