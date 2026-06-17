#pragma once
// ===========================================================================
// WandCompileSystem.h
// -------------------
// @file
// @brief isDirty な WandBuildComponent を検出し、コンパイルして
//        WandProgramRegistry へ登録、cachedProgramId を確定させる駆動System。
//
// 実行ステージ:
//   弾発射(WandFireSystem, L02)より前に完了している必要があるため L01 に置く。
//   ショップ側はビルド編集後に isDirty=true を立てるだけでよい（疎結合）。
// ===========================================================================

#include "Engine/ECS/System/CCL_IfSystem.h" // ※include パスはプロジェクト構成に合わせる
#include "Game/Logics/Wand/Compiler/WandBuildComponent.h"

/**
 * @brief 杖ビルドのコンパイル駆動System。書き込み対象は WandBuildComponent。
 * @note Resource(WandProgramRegistry) を書き換えるため並列実行はしない（ForEach を使用）。
 */
class WandCompileSystem
    : public CCL::ECS::IfSystem<WandCompileSystem, CCL::ECS::Write<WandBuildComponent>>
{
public:
    WandCompileSystem() : IfSystem("WandCompileSystem") {}

    /**
     * @brief dirty なビルドを全てコンパイル＆登録する。
     * @param dt 使用しない（コンパイルは時間非依存のイベント駆動処理のため）。
     */
    void Update(float dt) override;
};