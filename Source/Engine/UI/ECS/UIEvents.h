#pragma once
// UI発のイベント定義（EventBus で発火し、ゲーム側システムが購読する）。
#include "Engine/ECS/Common/CCL_Types.h"
#include <cstdint>

// ボタンがクリックされた時に発火。ゲーム側は actionHash で処理を分岐する。
struct UIButtonClickedEvent
{
    CCL::ECS::EntityID entity     = CCL::ECS::InvalidEntityID; // クリックされたボタンのエンティティ
    uint32_t           actionHash = 0;                         // UIButtonComponent.onClickAction のハッシュ
};
