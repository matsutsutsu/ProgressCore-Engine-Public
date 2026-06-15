#pragma once
// ===========================================================================
// Bullet/BulletSpawn.h
// --------------------
// @file
// @brief プログラムに従って弾を生成する共有ロジック（通常発射・継承の両方から呼ぶ）。
//        公開APIは Spawn 1本。Spawn命令(spawnOps / 継承=inheritanceOps)を解釈し、
//        Split に応じた本数を生成する。呼び出し側は「いつ撃つか」だけを判断する。
//
//   内部構造（BulletSpawn.cpp）:
//     ResolveSpawn  … <Name>Spawn ハンドラ群で集約(Pass1) + Synergy_*(Pass2)
//     BuildFan      … 確定本数・角度から扇方向を生成（純粋ジオメトリ）
//     SpawnOne      … 最終 dir/speed で弾を1発生成するプリミティブ
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
     * @brief spawn命令列を解釈し、Splitに応じた本数の弾を生成する。
     * @param spawnOps        spawn命令列（通常=prog.spawnOps / 継承=prog.inheritanceOps）
     * @param spawnOpCount    上記の有効数
     * @param baseDirection   照準の中心方向（未正規化可）
     * @param inheritanceDepth 0=通常発射, 1=継承で再射出された弾（再継承防止）
     * @return 実際に生成した弾数（Splitなしなら1）
     */
    int Spawn(
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