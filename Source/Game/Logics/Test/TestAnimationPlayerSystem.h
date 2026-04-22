#pragma once
#include "ECS/System/CCL_System.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Engine/GamePlay/Animation/AnimParametersComponent.h"
#include "TestAnimationPlayerComponent.h"

namespace CCL::ECS {

    class TestAnimationPlayerSystem : public IfSystem<TestAnimationPlayerSystem,
        Write<TransformComponent>,
        Write<AnimParametersComponent>,
        Read<TestAnimationPlayerComponent>>
    {
    public:
        TestAnimationPlayerSystem() : IfSystem("TestAnimationPlayerSystem") {}
        virtual ~TestAnimationPlayerSystem() = default;

        void Update(float dt) override;
    };

} // namespace CCL::ECS