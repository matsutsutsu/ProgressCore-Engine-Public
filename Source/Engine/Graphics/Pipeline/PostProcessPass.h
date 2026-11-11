#pragma once
// ===========================================================================
// 概要: HDR バッファにポストプロセスとトーンマップをかけ、最終の出力先に書き出すパス
//
// 責務:
//   - シーンの定数バッファをバインドし、PostProcessManager::EndScene を呼ぶ
//   - 深度と法線の SRV をエフェクトに渡す
//
// 備考: パイプラインの最後に置く
// ===========================================================================
#include "Engine/Graphics/Pipeline/Core/IRenderPass.h"

class PostProcessPass : public IRenderPass
{
public:
    void Execute(const RenderFrameContext &ctx) override;
};
