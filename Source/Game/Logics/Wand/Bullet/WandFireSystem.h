#pragma once
// ===========================================================================
// WandFireSystem.h
// ----------------
// @file
// @brief 発射意図とクールダウンを見て、cachedProgramId のプログラムに従い弾を生成するSystem。
//        Spawnフェーズの命令(現状は Accelerate=初速倍率)を解釈して初速・方向を確定する。
//
// 実行ステージ: L02_Update（コンパイル L01 の後。cachedProgramId は確定済み前提）。
// @note 弾生成は遅延適用される構造変更。共有 PendingOps に積むため並列化しない（ForEach）。
// ===========================================================================

#include "Engine/ECS/System/CCL_IfSystem.h"            // ※include パスはプロジェクト構成に合わせる
#include "Game/Logics/Wand/Compiler/WandBuildComponent.h"
#include "Game/Logics/Wand/Bullet/WandRuntimeComponents.h"
#include "Engine/Timer/TimeState.h" // ※TimeStateComponent のパスに合わせる

/**
 * @brief 杖発射System。Spawnフェーズを解釈して弾エンティティを生成する。
 */
class WandFireSystem
    : public CCL::ECS::IfSystem<WandFireSystem,
    CCL::ECS::Write<WandRuntimeComponent>,
    CCL::ECS::Read<WandBuildComponent>,
    CCL::ECS::Read<WandFireIntentComponent>,
    CCL::ECS::Read<TimeStateComponent>>
{
public:
    WandFireSystem() : IfSystem("WandFireSystem") {}

    /**
     * @brief クールダウン消化 → 発射判定 → Spawn解釈 → 弾生成。
     * @param dt 未使用（時間は per-entity の TimeStateComponent.localDt を使うため）。
     */
    void Update(float dt) override;
};