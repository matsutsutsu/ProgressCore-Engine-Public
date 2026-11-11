#pragma once
#include "Engine/Physics/JoltIntegration/PhysicsLayers.h"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/MotionType.h>
#include <DirectXMath.h>

// 剛体の物理パラメータ。Transform と形状のコライダーと一緒に付けると、JoltBodySetupSystem が剛体を作る
// 生成後に反映されるのは motionType だけで、ほかの値を変えても生成済みの剛体には効かない
struct JoltRigidbodyComponent
{
    JPH::EMotionType motionType  = JPH::EMotionType::Dynamic; // Static / Kinematic / Dynamic
    JPH::ObjectLayer objectLayer = PhysicsLayers::PLAYER;     // 衝突レイヤー（PhysicsLayers.h の一覧）
    float            restitution = 0.0f;                      // 反発係数
    float            friction    = 0.2f;                      // 摩擦係数

    // --- 弾や攻撃判定などのための設定 ---
    DirectX::XMFLOAT3 initialVelocity = {0.0f, 0.0f, 0.0f}; // 生成時の初速（m/秒）
    float             gravityFactor   = 1.0f;               // 重力の倍率（0 で無重力）
    bool              isSensor        = false;              // true なら物理的にはぶつからず、接触イベントだけ出す
    bool              useCCD          = false;              // true なら連続衝突判定（高速な弾の壁抜け防止）
};
