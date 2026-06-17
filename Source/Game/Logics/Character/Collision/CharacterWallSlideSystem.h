#pragma once
// ===========================================================================
// Systems/CharacterWallSlideSystem.h
// ------------------------------------
// @file
// @brief キャラの水平移動後に壁へのめり込みを DungeonGrid で押し出すSystem（L02末尾）。
//        Jolt CharacterVirtual と共存。Jolt が高さ/段差を担い、こちらは水平方向のみ。
//
// @note DungeonGrid が無いシーン（タイトル等）ではスキップ。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "CharacterWallComponents.h"

class CharacterWallSlideSystem
    : public CCL::ECS::IfSystem<CharacterWallSlideSystem,
    CCL::ECS::Write<TransformComponent>,
    CCL::ECS::Read<WallSlideTag>,
    CCL::ECS::Read<CharacterWallRadiusComponent>>
{
public:
    CharacterWallSlideSystem() : IfSystem("CharacterWallSlideSystem") {}
    void Update(float dt) override;
};