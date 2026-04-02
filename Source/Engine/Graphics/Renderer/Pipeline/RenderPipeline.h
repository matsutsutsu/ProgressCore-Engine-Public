#pragma once
#include <vector>
#include <memory>
#include "IRenderPass.h"

class RenderPipeline {
public:
    void AddPass(std::shared_ptr<IRenderPass> pass) {
        _passes.push_back(pass);
    }

    void InitializeAll(ID3D11Device* device) {
        for (auto& pass : _passes) {
            pass->Initialize(device);
        }
    }

    void ExecuteAll(const RenderContext& rc, RenderQueue& queue, ModelRenderer* renderer) {
        for (auto& pass : _passes) {
            pass->Execute(rc, queue, renderer);
        }
    }
private:
    std::vector<std::shared_ptr<IRenderPass>> _passes;
};