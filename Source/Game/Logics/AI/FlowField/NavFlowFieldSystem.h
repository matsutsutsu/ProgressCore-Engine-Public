/// @file  NavFlowFieldSystem.h
/// @brief プレイヤーのセルから BFS で距離場（NavGridFlowField）を毎フレーム計算するシステム。
///
/// ステージ: L01_Input
///   Resource 依存はスケジューラの Read/Write グラフに乗らないため、
///   「移動(L02)より前」を保証する目的で意図的に L01 に置く。
///
/// このSystemは ForEach を使わず、Update 内で
///   1) DungeonGrid Resource を読む
///   2) プレイヤー位置を View で取得
///   3) BFS で距離場を埋め、NavGridFlowField Resource に書く
/// という「ワールド単位の1パス」を行う。
#pragma once
#include "Engine/ECS/System/CCL_System.h"
#include "Game/Logics/Character/Player/PlayerComponent.h"   // パスは適宜合わせてください
#include <vector>

// ※ namespace で囲まない（プロジェクト規約）
// Read<PlayerComponent> は「プレイヤーを読む」依存の明示＋空フィルタ回避用。
// 実際の探索は Update 内で World::View / GetResource を使って行う。
class NavFlowFieldSystem : public CCL::ECS::IfSystem<
    NavFlowFieldSystem,
    CCL::ECS::Read<PlayerComponent>>
{
public:
    NavFlowFieldSystem();
    void Update(float dt) override;

private:
    // BFS 用キュー。毎フレーム clear して再利用（capacity を保持しヒープ確保を避ける）。
    std::vector<int> _bfsQueue;
};