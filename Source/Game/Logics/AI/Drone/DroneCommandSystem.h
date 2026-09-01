// ボスの命令（BossCommandComponent）を全ドローンへ配布する「伝令」システム。
//
// 役割分担（Manager が"何を"、個体が"どうやって"を持つ構成の、伝令にあたる部分）:
//   DroneManagerSystem   … 何機いるか（戸籍係）
//   DroneCommandSystem   … ボスの命令を全機へ配る（伝令）      ← このファイル
//   DroneFormationSystem … どこへ並ぶか（振付師）
//   DroneBrainSystem     … 命令をどう実行するか（個体判断）
//
// なぜ独立システムか:
//   ボスBT は BossCommandComponent（自分の出力）にだけ書き込み、1000機のドローンを
//   直接触らない（神クラス化の回避）。命令をドローン個体の入力へ変換する一点を
//   このシステムに集約することで、BT・Formation・判断層の責務が汚れない。
//
// データフロー:
//   BossCommandComponent.requestFormation  → 各 DroneComponent.currentFormation
//   BossCommandComponent.droneAttackActive → 各 DroneComponent.wantsAttack
#pragma once
#include <vector>
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"

// ボスの陣形・攻撃命令を配下ドローン全機へ毎フレーム配布する。
class DroneCommandSystem : public CCL::ECS::IfSystem<DroneCommandSystem,
      CCL::ECS::Write<DroneComponent>>
{
    using Base = CCL::ECS::IfSystem<DroneCommandSystem, CCL::ECS::Write<DroneComponent>>;

  public:
    DroneCommandSystem()
        : IfSystem("DroneCommandSystem")
    {
    }

    // BossCommandComponent はテンプレート引数に足せない。
    //
    // Read<>/Write<> はスケジューラへの申告であると同時に ForEach の絞り込み条件でもあり、
    // ここへ足すと「DroneComponent と BossCommandComponent を両方持つ Entity」だけが
    // 対象になってドローンが1機も回らなくなる。
    // 申告だけを足したいので、テンプレートは触らずこのメソッドで上書きする。
    //
    // 実際に触っているもの: requestFormation / droneAttackActive を読み、
    // 74行目で prevDroneAttackActive を書く（エッジ検出のため）。
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override
    {
        auto types = Base::GetWriteTypes();
        types.push_back(CCL::ECS::TypeInfo<BossCommandComponent>::ID());
        return types;
    }

    // ボスBTが今フレーム書いた命令を読む。BTより後に走らないと1フレーム古い命令を配る。
    std::vector<const char *> GetRunAfter() const override { return {"BehaviorTreeSystem"}; }

    void Update(float dt) override;
};
