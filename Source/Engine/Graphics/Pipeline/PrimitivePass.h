#pragma once
#include "Engine/Graphics/Pipeline/IRenderPass.h"

class PrimitivePass : public IRenderPass {
public:
    void Execute(const RenderContext& rc)override;
};