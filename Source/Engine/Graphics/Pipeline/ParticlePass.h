#pragma once
#include "Engine/Graphics/Pipeline/IRenderPass.h"

class ParticlePass : public IRenderPass {
public:
    void Execute(const RenderContext& rc)override;
};