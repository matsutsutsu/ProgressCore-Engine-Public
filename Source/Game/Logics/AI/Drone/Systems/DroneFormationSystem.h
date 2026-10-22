#pragma once
// ===========================================================================
// 概要: 各ドローンの currentFormation から陣形を引き、目標座標を
//       計算する System。
//
// 責務:
//   - 起動時に全陣形（C++ 実装とデータ陣形）を FormationRegistry へ登録する
//   - ボス位置・プレイヤー位置・狙う向きなどを FormationContext にまとめる
//   - 陣形の Calculate を呼び、予告の溜め（shiverTime / telegraphTime）を進める
//   - ボスに付いていく陣形には、ボスの速度を先渡しの速度（followFeedForward）として配る
//
// 備考:
//   TargetPositionを計算するだけで
// 　実際に動かすのは DroneMovementSystem。
//
//   陣形の仮想関数は機体ごとに呼ぶ。全機がほぼ同じ陣形なので分岐予測が当たり、コストは小さい。
//   ドローンのループ自体は DroneComponent の配列を連続アクセスのまま回す。
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
     * DroneCommand が drone.currentFormation を書き、こちらがそれを読む。
     *
     * DroneManagerSystem を足してあるのは、スポーン時の ReindexAllDrones() が localIndex / totalDrones を書き直すため。
     * 陣形はそれを添字にして座標を出す。
     */
    std::vector<const char *> GetRunAfter() const override
    {
        return {"BehaviorTreeSystem", "DroneCommandSystem", "SquadUpdateSystem",
                "DroneManagerSystem"};
    }

    /**
     * @brief テンプレート引数以外に読む型を申告する。
     *
     * MakeContext がボスの Transform と BossCommandComponent を View で引くので申告する。
     * テンプレート引数には足せない。ForEach の絞り込みまで変わってしまう。
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

    // System Manager 内に出す調整 UI（ボスの速度を乗せる割合）。
    void OnGui() override;

private:
    /**
     * @brief 陣形の計算に渡す情報（ボス位置・プレイヤー位置など）を組み立てる。
     *
     * 狙いの固定（m_lockedAimDir）を更新するので const ではない。
     *
     * @param dt 経過時間（秒）。陣形の中心の速度を測るのに使う。
     * @return 今フレームの陣形計算に渡す情報。
     */
    CCL::AI::Formation::FormationContext MakeContext(float dt);

    float m_elapsed = 0.0f; // 陣形の回転に使う経過時間 [秒]

    // 狙う向きの固定。信号の立ち上がり検出は DroneCommandSystem も持っているが、そちらの
    // 結果を使うと実行順によって固定される向きが1フレームずれる。陣形へ配るのはこの
    // System なので、ここで検出して固定する。MakeContext と同じく単一のボスを前提にしている。
    DirectX::SimpleMath::Vector3 m_lockedAimDir     = DirectX::SimpleMath::Vector3::UnitZ;
    bool                         m_aimLocked        = false;
    bool                         m_prevAttackActive = false;

    // ボスの速度を測るための前フレームの陣形の中心。ボスが居ないフレームを挟んだら測り直す。
    //   地面の高さに揃えた中心と、ボスの実際の位置の2つを別々に測る（IFormation::FollowsBossHeight）。
    DirectX::SimpleMath::Vector3 m_prevBossPos    = DirectX::SimpleMath::Vector3::Zero;
    DirectX::SimpleMath::Vector3 m_prevBodyPos    = DirectX::SimpleMath::Vector3::Zero;
    bool                         m_hasPrevBossPos = false;

    // ボスの実際の位置と速度（跳躍中も持ち上げたまま）。MakeContext が書き、Update が
    // FollowsBossHeight の陣形へ渡す情報を組むのに使う。
    DirectX::SimpleMath::Vector3 m_bossBodyPos      = DirectX::SimpleMath::Vector3::Zero;
    DirectX::SimpleMath::Vector3 m_bossBodyVelocity = DirectX::SimpleMath::Vector3::Zero;

    // ボスの速度をドローンへ乗せる割合。1 でボスと一緒に動き、下げるほど少し遅れて引きずられる。
    //   完全に一緒に動くと、ボスとドローンが1つの塊に見えて「群れ」らしさが消えるので、少しだけ下げる。
    float m_bossCarry = 0.85f;
};
