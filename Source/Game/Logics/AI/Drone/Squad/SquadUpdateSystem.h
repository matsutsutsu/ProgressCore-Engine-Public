#pragma once
// ===========================================================================
// 概要: 小隊（5機編隊）のアンカーを動かす System。小隊単位の判断はここに集める。
//
// 責務:
//   - 各小隊の生存数と、各席の前フレームの位置・速度を集める
//   - 段階（Regroup / Gather / Chase）を決め、アンカーを動かす
//   - 生成された機を小隊の空き席へ入れる（AssignNewMembers。DroneManagerSystem が生成の後に呼ぶ）
//   - 小隊の調整値と一覧のデバッグ UI
//
// 備考:
//   ドローン系 System の分担は次のとおり。
//     DroneCommandSystem   … ボスの命令を各機へ配る
//     SquadUpdateSystem    … 小隊のアンカーをどこへ動かすか決める（この System）
//     DroneFormationSystem … アンカーを起点に各機の持ち場を出す（Formations_Squad.h）
//     DroneBrainSystem     … 個体が今どうすべきか決める
//     DroneMovementSystem  … 実際に動かす
//
//   独立した System にしているのは、小隊が最大32個なのに対しドローンは1000機あるため。
//   機体ごとのループで行うと同じ計算を5回繰り返し、同じ小隊へ並列に書き込むことになる。
//
//   単スレッドで回し、SquadState への書き込みはここだけに限る。
//   段階ごとの処理と調整値は .cpp にまとめてある。
// ===========================================================================
#include <vector>

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"
#include "Game/Logics/AI/Drone/Squad/SquadState.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"

namespace CCL::ECS { class World; }

class SquadUpdateSystem : public CCL::ECS::IfSystem<
                              SquadUpdateSystem,
                              CCL::ECS::Read<DroneComponent>,
                              CCL::ECS::Read<TransformComponent>>
{
    using Base = CCL::ECS::IfSystem<SquadUpdateSystem,
                                    CCL::ECS::Read<DroneComponent>,
                                    CCL::ECS::Read<TransformComponent>>;

public:
    SquadUpdateSystem() : IfSystem("SquadUpdateSystem") {}

    void Initialize() override;

    /**
     * @brief 同じステージで先に走らせる System を宣言する。
     *
     * DroneCommandSystem が配った陣形指示を読んでから小隊の行動を決める。
     *
     * BehaviorTreeSystem も直接名指しする。間接的に後ろになるだけでは、起動時に「順序不定」と報告される。
     *
     * DroneManagerSystem は初回配備で squadId / slotInSquad を書くので、その後に走る。
     */
    std::vector<const char *> GetRunAfter() const override
    {
        return {"BehaviorTreeSystem", "DroneCommandSystem", "DroneManagerSystem"};
    }

    /**
     * @brief テンプレート引数以外に読む型を申告する。
     *
     * Update がボスの BossCommandComponent を View で引く。
     * テンプレート引数には足せない（ForEach の絞り込み条件を兼ねているため）。
     */
    std::vector<CCL::ECS::TypeID> GetReadTypes() const override
    {
        auto types = Base::GetReadTypes();
        types.push_back(CCL::ECS::TypeInfo<BossCommandComponent>::ID());
        return types;
    }

    void Update(float dt) override;

    void OnGui() override; // 小隊の調整値と、生きている小隊の一覧

    /**
     * @brief まだ小隊に入っていない機を、空いている席へ入れる。
     *
     * DroneManagerSystem が生成の直後に呼ぶ（書き込みは呼んだ側の System の中で行われる）。
     * 撃破の後には呼ばれないので、撃破で空いた席は埋め直さない（隊列は欠員を飛ばして詰める）。
     *
     * 所属は一度決めたら維持する。
     * 振り直すと生き残った機の所属まで入れ替わり、同じ部隊で組んでいるように見えない。
     *
     * @param world ドローンが居る World。
     */
    static void AssignNewMembers(CCL::ECS::World &world);

};
