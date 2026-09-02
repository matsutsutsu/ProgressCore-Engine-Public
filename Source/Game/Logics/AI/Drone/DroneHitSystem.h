// 突撃(FireCharge リリース)中のドローンがプレイヤーに触れたら被弾させるシステム。
//
// 役割分担（判断・実行・当たり判定のうち、当たり判定を担う）:
//   DroneBrainSystem       … いつ攻撃するか(currentState=FireCharge)を決める
//   DroneMovementSystem    … タメ→リリースの物理挙動を動かす
//   DroneHitSystem(本ファイル) … リリース中に触れたら DamageEvent を積む ← ここ
//
// なぜ独立システムか / なぜ距離判定か:
//   1000機ぶんの「攻撃用 Jolt センサー剛体」を持たせるのはコスト過大。ドローンは
//   BattleContext のプレイヤー座標を見て距離判定するだけにし、命中は既存の戦闘
//   パイプライン(DamageEvent→DamageResolution)へ1点合流させる。物理レイヤーや
//   HitboxCollisionSystem には一切触れない。
#pragma once
#include <vector>

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"
#include "Game/Logics/Combat/HealthComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"

// 突撃中ドローン×プレイヤーの接触を距離で判定し、DamageEvent を生成する。
class DroneHitSystem : public CCL::ECS::IfSystem<DroneHitSystem,
                           CCL::ECS::Read<DroneComponent>,
                           CCL::ECS::Read<TransformComponent>>
{
    using Base = CCL::ECS::IfSystem<DroneHitSystem,
                                    CCL::ECS::Read<DroneComponent>,
                                    CCL::ECS::Read<TransformComponent>>;

public:
    DroneHitSystem() : IfSystem("DroneHitSystem") {}

    // Update がプレイヤーの HealthComponent を GetComponent で読む（無敵中は走査ごと飛ばす）。
    //
    // テンプレート引数には足せない。あちらは ForEach の絞り込み条件も兼ねているので、
    // 足すと「ドローンかつ HealthComponent を持つ Entity」だけが対象になり1機も回らない。
    // 今は L05_Collision に HealthComponent を書くシステムが居らずステージの壁で
    // 直列化されているが、宣言に出ていない読み取りは依存解析から消えるので明示する。
    std::vector<CCL::ECS::TypeID> GetReadTypes() const override
    {
        auto types = Base::GetReadTypes();
        types.push_back(CCL::ECS::TypeInfo<HealthComponent>::ID());
        return types;
    }

    void Update(float dt) override;
};
