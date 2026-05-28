#pragma once
#include "Engine/Graphics/Pipeline/IRenderPass.h"

/// @brief ShapeRenderer / PrimitiveRenderer によるデバッグ描画パス（ワールド空間の末尾）。
class PrimitivePass : public IRenderPass
{
public:
    void Execute(const RenderFrameContext& ctx) override;
    const char* GetName() const override { return "PrimitivePass"; }
};