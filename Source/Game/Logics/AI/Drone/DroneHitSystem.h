/// @file  DroneHitSystem.h
/// @brief 突撃(FireCharge リリース)中のドローンがプレイヤーに触れたら被弾させるシステム。
///
/// 役割分担（判断・実行・当たり判定のうち、当たり判定を担う）:
///   DroneBrainSystem       … いつ攻撃するか(currentState=FireCharge)を決める
///   DroneMovementSystem    … タメ→リリースの物理挙動を動かす
///   DroneHitSystem(本ファイル) … リリース中に触れたら DamageEvent を積む ← ここ
///
/// なぜ独立システムか / なぜ距離判定か:
///   1000機ぶんの「攻撃用 Jolt センサー剛体」を持たせるのはコスト過大。ドローンは
///   BattleContext のプレイヤー座標を見て距離判定するだけにし、命中は既存の戦闘
///   パイプライン(DamageEvent→DamageResolution)へ1点合流させる。物理レイヤーや
///   HitboxCollisionSystem には一切触れない。
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"

/// @brief 突撃中ドローン×プレイヤーの接触を距離で判定し、DamageEvent を生成する。
class DroneHitSystem : public CCL::ECS::IfSystem<DroneHitSystem,
                           CCL::ECS::Read<DroneComponent>,
                           CCL::ECS::Read<TransformComponent>>
{
public:
    DroneHitSystem() : IfSystem("DroneHitSystem") {}

    void Update(float dt) override;
};
