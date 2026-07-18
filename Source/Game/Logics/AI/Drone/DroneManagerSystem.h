/// @file  DroneManagerSystem.h
/// @brief ドローンの「生死・数」を管理する司令塔システム。
///
/// 責務（企画書の「陣形Manager」とは別物）:
///   - Spawn   : ドローンをプレハブから生成する
///   - Count   : 生存数を数えて BattleContext.droneAliveCount に書き込む（＝ボスHP）
///   - Destroy : ドローンを破壊する（デバッグ・被弾処理の受け皿）
///
/// 役割分担:
///   DroneManagerSystem   … 何機いるか（戸籍係）  ← このファイル
///   DroneFormationSystem … どう並ぶか（振付師）  ← 既存・陣形計算に専念させる
///
/// これにより「ドローン数 = ボスHP」で司令官StateTreeの戦術が切り替わる。
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"
#include "Game/Logics/AI/Drone/DroneAI.h"
#include "Game/Logics/AI/StateTree/Core/StateTreeRegistry.h"
#include "Engine/Timer/TimeState.h"
#include "Game/Logics/GameEvent/GameEvents.h"

namespace CCL::ECS
{
class World;
}

/// @brief ドローンの生成・計数・破壊を一元管理するシステム。
///        毎フレーム生存数を数えて BattleContext に反映する。
class DroneManagerSystem : public CCL::ECS::IfSystem<DroneManagerSystem,
      CCL::ECS::Read<DroneComponent>> // 数えるだけなので Read
{
  public:
    DroneManagerSystem()
        : IfSystem("DroneManagerSystem")
    {
        hasGui = true;
    }

    void Initialize() override;

    /// @brief 毎フレーム：生存ドローンを数えて BattleContext.droneAliveCount に書く。
    void Update(float dt) override;

    // ----------------------------------------------------------------
    // 外部（GUI やゲームロジック）から呼ぶ操作
    // ----------------------------------------------------------------

    /// @brief ドローンを count 機スポーンする。
    /// @param count      生成数
    /// @param basePos    生成中心（通常はボス位置）。nullptr ならワールド原点付近
    /// @return 実際に生成できた数
    int SpawnDrones(int count);

    /// @brief ドローンを count 機破壊する（デバッグ用・被弾処理の暫定）。
    /// @return 実際に破壊した数
    int DestroyDrones(int count);

    /// @brief 全ドローンを破壊する。
    void DestroyAll();

    /// @brief 現在の生存数を返す（View で数える）。
    int GetAliveCount() const;
    
    /// @brief 全ドローンに対して localIndex / totalDrones を振り直す。
    void ReindexAllDrones();

    /// @brief 全ドローンの currentState を一括変更する（デバッグ用）。
    void SetAllDroneState(DroneState state);

    /// @brief 指定した1機のドローンを破壊する。
    ///        DroneComponent を持たない/既に破壊済みの EntityID を渡しても安全（何もしない）。
    /// @return 破壊できたら true
    bool DestroyDrone(CCL::ECS::EntityID droneId);

  private:
    /// @brief DroneAI アセットを StateTreeRegistry に登録する（未登録時のみ）。
    ///        EditorScene が Registry を再生成する順序問題を吸収するため遅延登録。
    void EnsureDroneAIRegistered();

    /// @brief JustEvadeSuccessEvent を受けたときの処理
    void OnJustEvadeSuccess(const struct JustEvadeSuccessEvent &ev);

    /// @brief シーン内の最初のボス（BossCommandComponent 保持）を探す。
    CCL::ECS::EntityID FindBoss() const;

    // GUI 用パラメータ（DroneFormationSystem から移設）
    int   _spawnCount  = 12;
    float _spawnRadius = 8.0f;
    float _spawnSpeed  = 40.0f;

  public:
    void OnGui() override; // 生成/破壊のデバッグUI
};