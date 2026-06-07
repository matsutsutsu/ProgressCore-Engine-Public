#pragma once
// ===========================================================================
// BulletComponents.h
// ------------------
// @file
// @brief 全ての魔法弾が共有する「単一 Archetype」を構成するランタイムコンポーネント群。
//
// 重要原則:
//   弾の見た目・挙動の多様性は【ProgramID が指す CompiledWandProgram のデータ】が
//   全て担う。コンポーネント構成(Archetype)は弾ごとに一切変えない。
//   → 全弾が同一 Chunk に密集 → SoA キャッシュ効率最大・ForEachParallel が全開。
//
// 単一弾 Archetype（想定構成）:
//   { TransformComponent, BulletProgramRef, BulletStateComponent,
//     BulletTag, RuntimeOnlyTag, (衝突用コンポーネント:後工程) }
//
// @note いずれも実行時専用。既存の RuntimeOnlyTag を併用してシリアライザに弾をスキップさせる。
// ===========================================================================

#include "WandTypes.h"
#include "SimpleMath.h"                   // DirectX::SimpleMath::Vector3
#include "Engine/ECS/Common/CCL_Types.h" // EntityID / InvalidEntityID（※実際のパスは要確認）

namespace RuneForge
{
    /**
     * @brief 弾がどのコンパイル済みプログラムに従うかを指す軽量参照。
     * @note これだけが「弾の個性」。実体(CompiledWandProgram)は Registry 側に1つだけ存在。
     */
    struct BulletProgramRef
    {
        ProgramID programId = InvalidProgramID;
    };

    /**
     * @brief 弾の毎フレーム書き換わるランタイム状態。
     *
     * @note direction(向き)と speed(速さ)を分離して持つ。Accelerate/Slow は speed を、
     *       Homing は direction を独立に操作する概念であり、分離した方が DOD 的に素直なため。
     *       位置は既存 TransformComponent が持つ（ここでは複製しない）。
     */
    struct BulletStateComponent
    {
        DirectX::SimpleMath::Vector3 direction = { 0.0f, 0.0f, 1.0f }; ///< 進行方向(正規化想定)
        float    speed = 0.0f; ///< 現在の速さ
        float    age = 0.0f; ///< 経過時間(秒)
        float    lifetime = 3.0f; ///< 寿命(秒)。age>=lifetime で Inheritance 発動
        uint8_t  inheritanceDepth = 0;    ///< 継承で生成された世代深度（暴走防止・デバッグ用）
        CCL::ECS::EntityID lastHitTarget = CCL::ECS::InvalidEntityID; ///< 直近ヒット対象(多段ヒット制御)
    };

    /// @brief 「これは魔法弾である」ことを示す空タグ（System フィルタ用）。
    struct BulletTag {};

} // namespace RuneForge