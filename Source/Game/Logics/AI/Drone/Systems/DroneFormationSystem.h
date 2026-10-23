#pragma once
// ===========================================================================
// 概要: 各ドローンの currentFormation から陣形を引き、目標座標を
//       計算する System。
//
// 責務:
//   - 起動時に全陣形（C++ 実装とデータ陣形）を FormationRegistry へ登録する
//   - ボス位置・プレイヤー位置・狙う向きなどを FormationContext にまとめる
//   - 陣形の Calculate を呼び、予告の時計（telegraphTime）を進める
//   - ボスに付いていく陣形には、ボスの速度を先渡しの速度（followFeedForward）として配る
//
// 備考:
//   TargetPositionを計算するだけで
// 　実際に動かすのは DroneMovementSystem。
// ===========================================================================
#include <vector>

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"
#include "Game/Logics/AI/Drone/Formation/IFormation.h"
#include "Game/Logics/Character/Boss/Components/BossActionComponent.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"

class DroneFormationSystem : public CCL::ECS::IfSystem<DroneFormationSystem,
                                                       CCL::ECS::Write<DroneComponent>>
{
    using Filter = CCL::ECS::IfSystem<DroneFormationSystem, CCL::ECS::Write<DroneComponent>>;

public:
    DroneFormationSystem() : IfSystem("DroneFormationSystem") {}

    void Initialize() override;

    /**
     * @brief 同じステージで先に走らせる System を宣言する。
     *
     * 陣形は、命令（currentFormation）・小隊の状態・通し番号（生成時に振り直す）を読んで持ち場を出す。
     * 後に走らないと、生成した最初のフレームだけ古い番号で円が組まれる。
     */
    std::vector<const char *> GetRunAfter() const override
    {
        return {"BehaviorTreeSystem", "DroneCommandSystem", "SquadUpdateSystem",
                "DroneManagerSystem"};
    }

    /**
     * @brief テンプレート引数以外に読む型（ボスの Transform と命令）を申告する。
     *
     * テンプレート引数に足すと ForEach の絞り込み条件まで変わってしまう。
     */
    std::vector<CCL::ECS::TypeID> GetReadTypes() const override
    {
        auto types = Filter::GetReadTypes();
        types.push_back(CCL::ECS::TypeInfo<BossCommandComponent>::ID());
        types.push_back(CCL::ECS::TypeInfo<BossActionComponent>::ID()); // 浮いているかを見る
        types.push_back(CCL::ECS::TypeInfo<TransformComponent>::ID());
        return types;
    }

    void Update(float dt) override;

    void OnGui() override; // ボスの速度を乗せる割合の調整

private:
    /**
     * @brief 陣形の計算に渡す情報（ボス位置・プレイヤー位置など）を組み立てる。
     * @param dt 経過時間 [秒]。陣形の中心の速度を測るのに使う。
     * @return 今フレームの陣形計算に渡す情報。
     * @note 狙いの固定（m_lockedAimDir）も更新する。
     */
    CCL::AI::Formation::FormationContext MakeContext(float dt);

    float m_elapsed = 0.0f; // 陣形の回転に使う経過時間 [秒]

    // --- 狙う向きの固定（撃つ合図の立ち上がりで固定）---
    DirectX::SimpleMath::Vector3 m_lockedAimDir     = DirectX::SimpleMath::Vector3::UnitZ; // 固定した向き
    bool                         m_aimLocked        = false;                               // 固定中か
    bool                         m_prevAttackActive = false;                               // 前フレームの撃つ合図

    // --- ボスの速度の測定（前フレームとの差から出す）---
    DirectX::SimpleMath::Vector3 m_prevBossPos    = DirectX::SimpleMath::Vector3::Zero; // 前フレームの陣形の中心（地面の高さに揃えたもの）
    DirectX::SimpleMath::Vector3 m_prevBodyPos    = DirectX::SimpleMath::Vector3::Zero; // 前フレームのボスの実際の位置
    bool                         m_hasPrevBossPos = false;                              // 前フレームにボスが居たか

    DirectX::SimpleMath::Vector3 m_bossBodyPos      = DirectX::SimpleMath::Vector3::Zero; // ボスの実際の位置（跳躍中も持ち上げたまま）
    DirectX::SimpleMath::Vector3 m_bossBodyVelocity = DirectX::SimpleMath::Vector3::Zero; // その速度

    float m_bossCarry = 0.85f; // ボスの速度を乗せる割合。1 未満にして、ボスと1つの塊に見えないようにする
};
