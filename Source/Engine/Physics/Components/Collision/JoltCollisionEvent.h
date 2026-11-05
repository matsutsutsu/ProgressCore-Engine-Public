#pragma once
#include "Engine/ECS/Common/CCL_Types.h"
#include <DirectXMath.h>

// Jolt の接触1件を EventBus へ流すイベント
// 接触リスナーが物理の更新中に溜め、JoltCollisionEventSystem が L05 で配る
struct JoltCollisionEvent
{
    CCL::ECS::EntityID entityA;         // 接触した片方（キャラクターの接触では常にキャラクター自身）
    CCL::ECS::EntityID entityB;         // 接触したもう片方
    DirectX::XMFLOAT3  contactPosition; // 接触点のワールド座標
    DirectX::XMFLOAT3  contactNormal;   // 接触面の法線

    // 接触時の相対速度（A の速度 - B の速度、m/秒）
    DirectX::XMFLOAT3 relativeVelocity = {0.0f, 0.0f, 0.0f};
};
