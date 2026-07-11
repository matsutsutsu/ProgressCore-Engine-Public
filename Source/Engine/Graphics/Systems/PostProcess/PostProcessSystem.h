#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"

#include "Engine/Graphics/Components/PostProcess/BloomConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/ToneMapConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/DistortionConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/RadialBlurConfigComponent.h"

// 前方宣言
class PostProcessManager;

class PostProcessSystem : public CCL::ECS::SystemBase {
public:
    // マネージャーを受け取るコンストラクタ
    PostProcessSystem()
        : SystemBase("PostProcessSystem")
    {
    }

    // どのデータにアクセスするかをスケジューラに自己申告する
    std::vector<CCL::ECS::TypeID> GetReadTypes() const override
    {
        return {CCL::ECS::TypeInfo<BloomConfigComponent>::ID(),
            CCL::ECS::TypeInfo<ToneMapConfigComponent>::ID(),
            CCL::ECS::TypeInfo<DistortionConfigComponent>::ID(),
            CCL::ECS::TypeInfo<RadialBlurConfigComponent>::ID()};
    }

    void Update(float dt) override;

};