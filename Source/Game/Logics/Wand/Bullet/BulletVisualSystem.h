#pragma once
// ===========================================================================
// BulletVisualSystem.h
// --------------------
// 役割:
//   見た目未付与の弾(visualAttached==false)を見つけ、プログラムが指定する
//   パーティクルVFXをプールから借りて弾の子として接ぐシステム。
//
// 責務:
//   - 弾の生成(BulletSpawn)と見た目付与を分離し、見た目側の責務をここに集約する。
//   - どのVFXを貼るかは CompiledWandProgram.bulletVfxPath から読む(データ駆動)。
//
// アーキテクチャ上の位置付け:
//   実行ステージ L02_PostUpdate。WandFireSystem(L02_Update)が同フレームに生成した
//   新規弾を拾い、Prefab::SpawnParticle で子に接ぐ。以降の追従(L04)・描画(R08)・
//   死亡時のプール返却(L07 HierarchyCleanupSystem)は既存の仕組みに自動で乗る。
//
// 設計方針:
//   - BulletSpawn は見た目を一切知らない(純粋に弾データだけ生成)。見た目はこのSystemが
//     後付けすることで疎結合を保つ。VFX差し替え・将来の距離間引きもここだけで完結する。
//   - SpawnParticle は構造変更を伴うため、反復中には呼ばず対象IDを集めてから処理する。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Wand/Bullet/BulletComponents.h"

class BulletVisualSystem
    : public CCL::ECS::IfSystem<BulletVisualSystem,
    CCL::ECS::Write<BulletStateComponent>,
    CCL::ECS::Read<BulletProgramRef>,
    CCL::ECS::Read<BulletTag>>
{
public:
    BulletVisualSystem() : IfSystem("BulletVisualSystem") {}
    void Update(float dt) override;
};