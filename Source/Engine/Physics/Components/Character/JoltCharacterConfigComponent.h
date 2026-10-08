#pragma once
// ===========================================================================
// 概要: 仮想キャラクター（CharacterVirtual）の運動性能と物理レイヤーの設定。
//
// 責務:
//   - 登れる坂の角度・乗り越えられる段差などの地形との関わり方
//   - 歩行速度・ジャンプ・重力・加減速の手触り
//   - キャラクターの物理レイヤー（剛体と同じ ObjectLayer の仕組みで管理する）
//
// 備考: プレイヤーやボスに付けると、JoltCharacterSetupSystem がこれを見て
//       CharacterVirtual と presence 用の Kinematic プロキシ剛体を生成する。
//       生成後も walkSpeed などを PlayerMoveSystem / BossActionSystem が参照する。
// ===========================================================================
#include "Engine/Physics/JoltIntegration/PhysicsLayers.h"

struct JoltCharacterConfigComponent
{
    // --- 物理レイヤー ---
    // presence用KinematicプロキシのObjectLayer。ExtendedUpdateのフィルタにも使う。
    // これにより「キャラが世界を見る層」と「他者がキャラを見る層」が一致する。
    JPH::ObjectLayer objectLayer = PhysicsLayers::PLAYER;

    float maxSlopeAngle = 45.0f; // 登れる最大角度（度）
    float maxStepHeight = 0.5f;  // 乗り越えられる段差の最大高（m）
    float walkSpeed     = 6.0f;  // 歩行速度（m/秒）
    float jumpSpeed     = 10.0f; // ジャンプの初速（m/秒）
    float characterMass = 70.0f; // 体重（kg）。他の物体を押す力（mMaxStrength）の元になる

    // --- 重力（上昇より落下を速くして、着地を締める） ---
    float gravityScale     = 1.0f; // 上昇中の重力倍率
    float fallGravityScale = 2.5f; // 落下中の重力倍率

    // --- 加減速の手触り（水平移動の速度追従レート） ---
    // 目標速度へ寄せる Lerp のレート（/秒）。加速と減速で別々に持たせ、
    // 「素早く加速して、狙ったぶんだけ滑って止まる」を作れるようにする。
    float acceleration = 12.0f; // 加速のレート（高いほどすぐ最高速へ乗る）
    float deceleration = 8.0f;  // 減速のレート（低いほどダッシュ後によく滑る）

    // --- ジャンプ頂点付近の滞空（Apex Hang Time） ---
    float apexGravityScale      = 0.3f; // 頂点付近での重力倍率（小さいほど長く浮く）
    float apexVelocityThreshold = 1.5f; // 頂点付近とみなす Y 速度の絶対値の上限（m/秒）
};