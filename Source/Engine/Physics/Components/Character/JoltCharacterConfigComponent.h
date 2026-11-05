#pragma once
#include "Engine/Physics/JoltIntegration/PhysicsLayers.h"

// 仮想キャラクター（CharacterVirtual）の運動性能と物理レイヤー。付けると JoltCharacterSetupSystem が作る
// walkSpeed は普段の歩く速さで、足滑り補正の基準にもなる。技だけの速さは overrideSpeed で渡す
struct JoltCharacterConfigComponent
{
    // --- 物理レイヤー ---

    // プロキシ剛体の層。キャラが世界を見る層にも使い、両者を一致させる
    JPH::ObjectLayer objectLayer = PhysicsLayers::PLAYER;

    // --- 地形・速さ・体重 ---

    float maxSlopeAngle = 45.0f; // 登れる最大角度 [度]
    float maxStepHeight = 0.5f;  // 乗り越えられる段差 [m]
    float walkSpeed     = 6.0f;  // 普段の歩く速さ [m/s]
    float jumpSpeed     = 10.0f; // ジャンプの初速 [m/s]
    float characterMass = 70.0f; // 体重 [kg]。他の物体を押す力の元

    // --- 重力（上昇より落下を速くして、着地を締める）---

    float gravityScale     = 1.0f; // 上昇中の重力倍率
    float fallGravityScale = 2.5f; // 落下中の重力倍率

    // --- 加減速（目標の水平速度へ寄せる速さ [/秒]）---

    float acceleration = 12.0f; // 加速。高いほどすぐ最高速に乗る
    float deceleration = 8.0f;  // 減速。低いほどよく滑る

    // --- ジャンプの頂点で少し浮かせる ---

    float apexGravityScale      = 0.3f; // 頂点付近の重力倍率。小さいほど長く浮く
    float apexVelocityThreshold = 1.5f; // 頂点付近とみなす上下の速さ [m/s]
};
