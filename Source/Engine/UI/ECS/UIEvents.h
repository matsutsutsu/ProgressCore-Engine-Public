#pragma once
// UI が EventBus で発行するイベント。UI の操作結果をゲーム側の System へ伝える
#include "Engine/ECS/Common/CCL_Types.h"
#include <cstdint>

// ボタンがクリックされたときに UIButtonSystem が発行する。ゲーム側は actionHash で処理を分ける
struct UIButtonClickedEvent
{
    CCL::ECS::EntityID entity     = CCL::ECS::InvalidEntityID; // クリックされたボタンのエンティティ
    uint32_t           actionHash = 0;                         // UIButtonComponent.onClickAction のハッシュ
};
