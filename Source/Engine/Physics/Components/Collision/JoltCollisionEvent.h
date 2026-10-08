#pragma once
// ===========================================================================
// 概要: Jolt の接触1件を ECS の EventBus へ流すためのイベント。
//
// 責務:
//   - 接触した2つの Entity と、接触点・法線・相対速度を持つ
//
// 備考: 接触リスナーが物理更新中に溜め、JoltCollisionEventSystem が L05 で配送する。
// ===========================================================================
#include "Engine/ECS/Common/CCL_Types.h"
#include <DirectXMath.h>

struct JoltCollisionEvent
{
    CCL::ECS::EntityID entityA;         // 接触した片方（キャラクターの接触では常にキャラクター自身）
    CCL::ECS::EntityID entityB;         // 接触したもう片方
    DirectX::XMFLOAT3  contactPosition; // 接触点のワールド座標
    DirectX::XMFLOAT3  contactNormal;   // 接触面の法線

    // 接触時の相対速度（A の速度 - B の速度、m/秒）
    DirectX::XMFLOAT3 relativeVelocity = {0.0f, 0.0f, 0.0f};
};
