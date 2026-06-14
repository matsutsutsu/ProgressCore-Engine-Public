#pragma once
// ===========================================================================
// Systems/BulletSpawn.h
// ---------------------
// @file
// @brief 弾を1発生成する共有ロジック。通常発射と継承の両方から呼ぶ。
//        渡された spawn命令列(通常=spawnOps / 継承=inheritanceOps)を適用し、
//        プール上限を尊重して弾を生成する。
// ===========================================================================

#include "Game/Logics/Wand/Core/WandTypes.h"
#include "Game/Logics/Wand/Compiler/CompiledWandProgram.h"
#include "SimpleMath.h"
#include "Engine/ECS/Common/CCL_Types.h"

namespace CCL::ECS::Core { class World; }
class MagicBulletPool;

namespace BulletSpawn
{
    /**
     * @brief spawn命令列を適用しつつ弾を1発生成する（MagicBulletPool の上限を尊重）。
     * @param spawnOps        spawn時に解釈する命令列（通常=prog.spawnOps / 継承=prog.inheritanceOps）
     * @param spawnOpCount    上記の有効数
     * @param baseDirection   進行方向（未正規化可）
     * @param inheritanceDepth 0=通常発射, 1=継承で再射出された弾（再継承防止のため）
     * @return 生成した弾の EntityID
     */
    CCL::ECS::EntityID Spawn(
        CCL::ECS::Core::World* world,
        MagicBulletPool& pool,
        ProgramID programId,
        const CompiledWandProgram& prog,
        const EffectOp* spawnOps, uint8_t spawnOpCount,
        const DirectX::SimpleMath::Vector3& position,
        const DirectX::SimpleMath::Vector3& baseDirection,
        CCL::ECS::EntityID owner,
        uint8_t inheritanceDepth);
}