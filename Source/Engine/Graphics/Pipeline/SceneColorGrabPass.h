#pragma once
// ===========================================================================
// 概要: その時点の HDR バッファを別のテクスチャに写し、屈折・歪み用に t5 へバインドするパス。
//
// 責務:
//   - PostProcessManager::GrabSceneColor を呼ぶ
//
// 備考: 描いている最中のレンダーターゲットはシェーダーから読めないので、コピーを読ませる。
//       不透明とスカイボックスの後、半透明（TransparentPass）と TrailPass の前に置く。
// ===========================================================================
#include "Engine/Graphics/Pipeline/Core/IRenderPass.h"

class SceneColorGrabPass : public IRenderPass
{
public:
    void Execute(const RenderFrameContext &ctx) override;
};
