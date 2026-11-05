#pragma once
// ===========================================================================
// 概要: ShapeRenderer と PrimitiveRenderer でデバッグ用の図形を描くパス
//
// 責務:
//   - このフレームに積まれた図形を、カメラのビュー・プロジェクション行列で描く
//
// 備考: ワールド空間に描くものの最後（ポストプロセスの直前）に置く
// ===========================================================================
#include "Engine/Graphics/Pipeline/Core/IRenderPass.h"

class PrimitivePass : public IRenderPass
{
public:
    void Execute(const RenderFrameContext &ctx) override;
};
