#pragma once
#include "Engine/ECS/System/CCL_System.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Animation/Components/AnimParametersComponent.h"
#include "TestAnimationPlayerComponent.h"


    class TestAnimationPlayerSystem : public CCL::ECS::IfSystem<TestAnimationPlayerSystem,
        CCL::ECS::Write<TransformComponent>,
        CCL::ECS::Write<AnimParametersComponent>,
        CCL::ECS::Read<TestAnimationPlayerComponent>>
    {
    public:
        TestAnimationPlayerSystem() : IfSystem("TestAnimationPlayerSystem") {}
        virtual ~TestAnimationPlayerSystem() = default;

        void Update(float dt) override;
    };
