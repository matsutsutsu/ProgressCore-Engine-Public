#pragma once
#include "Engine/Graphics/Pipeline/Core/IRenderPass.h"

class SceneColorGrabPass : public IRenderPass
{
public:
    void Execute(const RenderFrameContext& ctx) override;
    const char* GetName() const override { return "SceneColorGrabPass"; }
};
