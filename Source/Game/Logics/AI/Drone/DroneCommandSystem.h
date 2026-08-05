/// @file  DroneCommandSystem.h
/// @brief ボスの命令（BossCommandComponent）を全ドローンへ配布する「伝令」システム。
///
/// 役割分担（Manager が"何を"、個体が"どうやって"を持つ構成の、伝令にあたる部分）:
///   DroneManagerSystem   … 何機いるか（戸籍係）
///   DroneCommandSystem   … ボスの命令を全機へ配る（伝令）      ← このファイル
///   DroneFormationSystem … どこへ並ぶか（振付師）
///   DroneAI(StateTree)   … 命令をどう実行するか（個体判断）
///
/// なぜ独立システムか:
///   ボスBT は BossCommandComponent（自分の出力）にだけ書き込み、1000機のドローンを
///   直接触らない（神クラス化の回避）。命令をドローン個体の入力へ変換する一点を
///   このシステムに集約することで、BT・Formation・StateTree の責務が汚れない。
///
/// データフロー:
///   BossCommandComponent.requestFormation  → 各 DroneComponent.currentFormation
///   BossCommandComponent.droneAttackActive → 各 DroneComponent.wantsAttack
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"

/// @brief ボスの陣形・攻撃命令を配下ドローン全機へ毎フレーム配布する。
class DroneCommandSystem : public CCL::ECS::IfSystem<DroneCommandSystem,
      CCL::ECS::Write<DroneComponent>>
{
  public:
    DroneCommandSystem()
        : IfSystem("DroneCommandSystem")
    {
    }

    void Update(float dt) override;
};
