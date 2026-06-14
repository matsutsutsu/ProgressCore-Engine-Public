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

/// @brief 壁スライド押し出しを受けるキャラに付けるタグ。
struct WallSlideTag {};

/// @brief キャラの当たり判定半径（XZ平面の円）。
struct CharacterRadiusComponent { float radius = 0.4f; };

class CharacterWallSlideSystem
    : public CCL::ECS::IfSystem<CharacterWallSlideSystem,
    CCL::ECS::Write<TransformComponent>,
    CCL::ECS::Read<WallSlideTag>,
    CCL::ECS::Read<CharacterRadiusComponent>>
{
public:
    CharacterWallSlideSystem() : IfSystem("CharacterWallSlideSystem") {}
    void Update(float dt) override;
};