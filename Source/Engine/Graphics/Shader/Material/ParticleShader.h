// ParticleShader.h
#pragma once
#include "Engine/Graphics/Shader/Core/Shader.h"

// パーティクル描画専用のシェーダーラッパー（ホットリロード対応）
class ParticleShader : public Shader {
public:
    ParticleShader(ID3D11Device* device);
    ~ParticleShader() override = default;

    void Begin(ID3D11DeviceContext* dc, const RenderServices& s, const RenderFrameContext& ctx) override;
    void Update(ID3D11DeviceContext* dc, const Model::Mesh& mesh, const MaterialData* overrideMaterial = nullptr) override {}
    void End(ID3D11DeviceContext* dc, const RenderServices& s) override;

    void CheckAndReload(ID3D11Device* device) override;

private:
    Microsoft::WRL::ComPtr<ID3D11VertexShader>   _vs;
    Microsoft::WRL::ComPtr<ID3D11GeometryShader> _gs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>    _ps;

    std::wstring _vsPath, _gsPath, _psPath;
    std::filesystem::file_time_type _vsTime, _gsTime, _psTime;

    void LoadShaders(ID3D11Device* device);
};