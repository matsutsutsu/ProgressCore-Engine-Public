#pragma once
// ===========================================================================
// 概要: 発表用デモ。隊長機を大量のドローンが追いかける群れで、ECS × JobSystem の並列処理を見せるシーン
//
// 責務:
//   - ParticleScene と同じ土台（ライト・カメラ）のレベルを読み込む
//   - 隊長機（ドローンのモデル）1機と、一般機（キューブ）を指定の数ぶん生成し、作り直す
//   - 機数・群れの更新時間・並列 ON/OFF を ImGui で表示・操作する
//
// 備考: エディタのシーン切り替えから "DemoSwarm" を開く
//       どの機体も AI も当たり判定も持たない（1万機ぶんの剛体を作ると物理の方が重くなるため）
// ===========================================================================
#include <vector>

#include "Engine/Scene/BaseScene.h"

class DemoSwarmSystem;

class DemoSwarmScene : public BaseScene
{
public:
    DemoSwarmScene() = default;
    ~DemoSwarmScene() override {};

    void Initialize() override;
    void Finalize() override;
    void Update(float elapsedTime) override;

protected:
    void RegisterSystems() override;
    void OnDrawImGui() override;

private:
    // 今の群れを消して、count 機で最初の塊から作り直す
    void RespawnSwarm(uint32_t count);

    DemoSwarmSystem *m_swarmSystem = nullptr; // 所有は SystemManager

    std::vector<CCL::ECS::EntityID> m_drones; // 生成した機体（作り直すときに消すため）

    int m_requestedCount = 1000; // ImGui で選んでいる機数
    int m_pendingCount   = -1;   // 次の Update で作り直す機数。-1 なら作り直さない
};
