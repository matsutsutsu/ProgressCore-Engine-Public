// 各ドローンの currentFormation を見て registry から陣形を引き、targetPosition を
// 計算するシステム。実際に動かすのは DroneMovementSystem。
#pragma once
#include <vector>

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"
#include "Game/Logics/AI/Drone/Formation/IFormation.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"

class DroneFormationSystem : public CCL::ECS::IfSystem<DroneFormationSystem,
                                 CCL::ECS::Write<DroneComponent>>
{
    using Base = CCL::ECS::IfSystem<DroneFormationSystem, CCL::ECS::Write<DroneComponent>>;

  public:
    DroneFormationSystem() : IfSystem("DroneFormationSystem"){}

    void Initialize() override;

    // DroneCommand が drone.currentFormation を書き、こちらがそれを読む。
    //   DroneManagerSystem を足してあるのは、スポーン時の ReindexAllDrones() が
    //   localIndex / totalDrones を書き直すため（陣形はそれを添字にして座標を出す）。
    std::vector<const char *> GetRunAfter() const override
    {
        return {"BehaviorTreeSystem", "DroneCommandSystem", "SquadUpdateSystem",
                "DroneManagerSystem"};
    }

    // MakeContext がボスの Transform と BossCommandComponent を View で引くので申告する
    // （テンプレート引数には足せない。ForEach の絞り込みまで変わってしまう）。
    std::vector<CCL::ECS::TypeID> GetReadTypes() const override
    {
        auto types = Base::GetReadTypes();
        types.push_back(CCL::ECS::TypeInfo<BossCommandComponent>::ID());
        types.push_back(CCL::ECS::TypeInfo<TransformComponent>::ID());
        return types;
    }

    void Update(float dt) override;

  private:
    /// ボス位置・プレイヤー位置など「場の情報」を組み立てる。
    /// 狙いの固定（下の m_lockedAimDir）を更新するので const ではない。
    CCL::AI::Formation::FormationContext MakeContext(float dt);

    float m_elapsed = 0.0f; // 回転アニメ用の経過時間

    // 狙う向きの固定。立ち上がり検出は DroneCommandSystem も持っているが、そちらに
    // 相乗りすると実行順で固定される向きが1フレームずれる。陣形へ配るのはこのシステム
    // なので、自分で見て自分で焼く。MakeContext と同じく単一のボスを前提にしている。
    DirectX::SimpleMath::Vector3 m_lockedAimDir     = DirectX::SimpleMath::Vector3::UnitZ;
    bool                         m_aimLocked        = false;
    bool                         m_prevAttackActive = false;
};
