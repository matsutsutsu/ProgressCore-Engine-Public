#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "LissajousOrbitComponent.h" // ※Componentの構造体定義ヘッダー
#include <cmath>

class LissajousOrbitSystem : public CCL::ECS::IfSystem<LissajousOrbitSystem,
    CCL::ECS::Write<TransformComponent>,
    CCL::ECS::Write<LissajousOrbitComponent>>
{
public:
    LissajousOrbitSystem() : IfSystem("LissajousOrbitSystem") {}

    void Update(float dt) override;
};