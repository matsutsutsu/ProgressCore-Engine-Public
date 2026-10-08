#pragma once
// ===========================================================================
// 概要: フレームの最初に HDR バッファをクリアし、描画先としてバインドするパス。
//
// 責務:
//   - PostProcessManager::BeginScene を呼ぶ
// ===========================================================================
#include "Engine/Graphics/Pipeline/Core/IRenderPass.h"

class SetupPass : public IRenderPass
{
public:
    void Execute(const RenderFrameContext &ctx) override;
};
