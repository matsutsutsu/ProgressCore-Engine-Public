#pragma once
#include "Engine/Graphics/Core/RenderContext.h"
#include "Engine/Graphics/Renderer/RenderQueue.h"

class ModelRenderer; // 循環参照を防ぐための前方宣言

class IRenderPass {
public:
    virtual ~IRenderPass() = default;

    virtual void Initialize(ID3D11Device* device) {}

    // ModelRendererのポインタを受け取り、必要な機能を呼び出せるようにする
    virtual void Execute(const RenderContext& rc, RenderQueue& queue, ModelRenderer* renderer) = 0;
};