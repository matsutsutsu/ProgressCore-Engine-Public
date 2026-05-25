#pragma once
#include "Engine/Graphics/Pipeline/IRenderPass.h"

class SetupPass : public IRenderPass {
public:
    void Execute(const RenderContext& rc)override;
};