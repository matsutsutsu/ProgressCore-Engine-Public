// ドローンの「生死・数」を管理する司令塔システム。生成・計数・破壊だけを持ち、
// 陣形計算は DroneFormationSystem、個体の判断は DroneBrainSystem が担う。
//
// 生存数を BattleContext.droneAliveCount に書くことで「ドローン数 = ボスHP」になり、
// ボスの戦術がその数で切り替わる。
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"
#include "Engine/Time/TimeState.h"

namespace CCL::ECS
{
class World;
}

class DroneManagerSystem : public CCL::ECS::IfSystem<DroneManagerSystem,
      CCL::ECS::Read<DroneComponent>> // 数えるだけなので Read
{
  public:
    DroneManagerSystem()
        : IfSystem("DroneManagerSystem")
    {
    }

    /// 毎フレーム、生存ドローンを数えて BattleContext.droneAliveCount に書く。
    void Update(float dt) override;

    // ---- 外部（GUI やゲームロジック）から呼ぶ操作 ----

    /// ドローンを count 機スポーンし、実際に生成できた数を返す。
    /// basePos は生成中心（通常はボス位置）。nullptr ならワールド原点付近。
    int SpawnDrones(int count);

    /// ドローンを count 機破壊し、実際に破壊した数を返す。
    int DestroyDrones(int count);

    /// 全ドローンを破壊する。
    void DestroyAll();

    /// 現在の生存数を返す（View で数える）。
    int GetAliveCount() const;
    
    /// 全ドローンに対して localIndex / totalDrones を振り直す。
    void ReindexAllDrones();

    /// 全ドローンの currentState を一括変更する（デバッグ用）。
    void SetAllDroneState(DroneState state);

  private:
    /// ボスが準備でき次第、初期ドローンを1回だけ配備する。シーン開始直後はボス未生成の
    /// フレームがあるため、揃うまで毎フレーム再試行する。
    void TryAutoSpawnInitial();

    /// シーン内の最初のボス（BossCommandComponent 保持）を探す。
    CCL::ECS::EntityID FindBoss() const;

    // GUI 用パラメータ。
    int   m_spawnCount  = 12;
    float m_spawnRadius = 8.0f;
    float m_spawnSpeed  = 40.0f;

    bool m_autoSpawnEnabled = true;  // シーン開始時に初期ドローンを自動配備するか
    bool m_autoSpawnDone    = false; // 初回配備が完了したか（1回だけ実行するためのラッチ）
    int  m_initialSpawnCount = 100;  // 初期配備数（展示会向け。スペックに応じて調整可）

  public:
    void OnGui() override; // 生成/破壊のデバッグUI
};