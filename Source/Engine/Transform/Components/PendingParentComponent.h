#pragma once
// ===========================================================================
// 概要: 親子付けを予約するコンポーネント PendingParentComponent。
//
// 責務:
//   - 生成直後でまだ SetParent できない Entity について、付けたい親を覚えておく
//
// 備考: TransformUpdateSystem が次の更新で SetParent を実行し、このコンポーネントを外す。
//       parentID が 0 / InvalidEntityID なら、親から切り離す予約になる。
// ===========================================================================
#include "Engine/ECS/Common/CCL_Types.h"

struct PendingParentComponent
{
    CCL::ECS::EntityID parentID = CCL::ECS::InvalidEntityID;
};
