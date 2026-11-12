#pragma once
#include "Engine/ECS/Common/CCL_Types.h"
#include <cstdint>

// どのボスの状態の間だけ出すか
// 並びを変えたら BossStatusVfxComponentMeta.cpp の BossStatusVfxKindNames も直す
enum class BossStatusVfxKind : uint8_t
{
    Rage,      // 怒り中
    Exhausted, // 疲れ
};

// ボスの子のパーティクルに付け、ボスがその状態の間だけ出させる
// 出し入れは BossStatusVfxSystem が GPUParticleComponent の emitting で行う
struct BossStatusVfxComponent
{
    BossStatusVfxKind kind = BossStatusVfxKind::Rage; // 出す状態

    // --- 実行時の状態（保存しない）---

    CCL::ECS::EntityID owner     = CCL::ECS::InvalidEntityID; // 状態を読むボス（祖先から探す）
    bool               wasActive = false;                     // 前のフレームに出していたか
};
