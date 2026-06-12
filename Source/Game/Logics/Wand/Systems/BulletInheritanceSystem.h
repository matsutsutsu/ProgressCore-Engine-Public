#pragma once
// ===========================================================================
// Systems/BulletInheritanceSystem.h
// ---------------------------------
// @file
// @brief 寿命(age>=lifetime)を迎えた弾を処理するSystem（L07_Cleanup・直列）。
//        深度0かつ継承命令を持つ弾は、死ぬ直前に新弾を再射出してから破棄する。
//
// ループ防止: 再射出弾は inheritanceDepth=1 で生まれ、深度>0は再継承しない。
//             ＝企画の「継承で生まれた弾は継承スロットが空」を深度ガードで実現。
// @warning 生成・破棄・プール操作を伴うため直列(ForEachWithID)。
// ===========================================================================

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Wand/Data/BulletComponents.h"
#include "Engine/Transform/Components/TransformComponent.h"

class BulletInheritanceSystem
    : public CCL::ECS::IfSystem<BulletInheritanceSystem,
    CCL::ECS::Read<TransformComponent>,
    CCL::ECS::Read<BulletStateComponent>,
    CCL::ECS::Read<BulletProgramRef>,
    CCL::ECS::Read<BulletTag>>
{
public:
    BulletInheritanceSystem() : IfSystem("BulletInheritanceSystem") {}
    void Update(float dt) override;
};