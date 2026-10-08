#pragma once
// ===========================================================================
// 概要: 剛体の物理パラメータ（動き方・レイヤー・材質・初速など）。Jolt のポインタは持たない。
//
// 責務:
//   - JoltBodySetupSystem が剛体を生成するときの設定値を持つ
//
// 備考: これと TransformComponent、形状（Box/Sphere/Capsule/Mesh のコライダー）を
//       Entity に付けると、JoltBodySetupSystem が Jolt 空間に剛体を生成する。
//       生成後に反映されるのは motionType だけ（JoltPushSystem が同期する）。
//       それ以外の値を変えても、生成済みの剛体には反映されない。
// ===========================================================================
#include "Engine/Physics/JoltIntegration/PhysicsLayers.h" // Manager 全体は引かず、レイヤー定義だけを使う
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/MotionType.h>
#include <DirectXMath.h>

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
