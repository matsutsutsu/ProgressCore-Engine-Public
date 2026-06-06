#pragma once
#include "Engine/Graphics/Pipeline/Core/IRenderPass.h"

/**
 * @brief フレーム冒頭で HDR バッファをクリアし、出力先としてバインドするパス。
 */
class SetupPass : public IRenderPass
{
public:
    void Execute(const RenderFrameContext& ctx) override;
    const char* GetName() const override { return "SetupPass"; }
};