#pragma once
#include "Engine/Graphics/Pipeline/IRenderPass.h"

class PostProcessPass : public IRenderPass {
public:
    void Execute(const RenderContext& rc)override;
};