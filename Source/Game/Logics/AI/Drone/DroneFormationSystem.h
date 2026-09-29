#pragma once
// ===========================================================================
// 概要: 各ドローンの currentFormation から陣形を引き、持ち場（targetPosition）を
//       計算する System。
//
// 責務:
//   - 起動時に全陣形（C++ 実装とデータ陣形）を FormationRegistry へ登録する
//   - ボス位置・プレイヤー位置・狙う向きなどを FormationContext にまとめる
//   - 陣形の Calculate を呼び、予告の溜め（windupTimer / telegraphTime）を進める
//
// 備考: 実際に動かすのは DroneMovementSystem。仮想呼び出しは陣形を引くときだけで、
//       ドローンのループ自体は連続アクセスのまま回す。
// ===========================================================================
#include <vector>

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"
#include "Game/Logics/AI/Drone/Formation/IFormation.h"
#include "Game/Logics/Character/Boss/Components/BossActionComponent.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"

class DroneFormationSystem : public CCL::ECS::IfSystem<DroneFormationSystem,
                                                       CCL::ECS::Write<DroneComponent>>
{
    using Base = CCL::ECS::IfSystem<DroneFormationSystem, CCL::ECS::Write<DroneComponent>>;

public:
    DroneFormationSystem() : IfSystem("DroneFormationSystem") {}

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
        types.push_back(CCL::ECS::TypeInfo<BossActionComponent>::ID()); // 浮いているかを見る
        types.push_back(CCL::ECS::TypeInfo<TransformComponent>::ID());
        return types;
    }

    void Update(float dt) override;

private:
    // ボス位置・プレイヤー位置などの情報を組み立てる。
    // 狙いの固定（下の m_lockedAimDir）を更新するので const ではない。
    CCL::AI::Formation::FormationContext MakeContext(float dt);

    float m_elapsed = 0.0f; // 陣形の回転に使う経過時間 [秒]

    // 狙う向きの固定。信号の立ち上がり検出は DroneCommandSystem も持っているが、そちらの
    // 結果を使うと実行順によって固定される向きが1フレームずれる。陣形へ配るのはこの
    // System なので、ここで検出して固定する。MakeContext と同じく単一のボスを前提にしている。
    DirectX::SimpleMath::Vector3 m_lockedAimDir     = DirectX::SimpleMath::Vector3::UnitZ;
    bool                         m_aimLocked        = false;
    bool                         m_prevAttackActive = false;
};
