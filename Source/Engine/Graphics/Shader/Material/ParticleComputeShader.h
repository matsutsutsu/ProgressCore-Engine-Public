#pragma once
#include "Engine/Graphics/Shader/Core/Shader.h"

// =========================================================================
// パーティクルシミュレーション（Compute Shader）専用の管理ラッパー
// 描画シェーダーと同様に ShaderRegistry に登録し、ホットリロードを可能にする
// =========================================================================
class ParticleComputeShader : public Shader {
public:
    ParticleComputeShader(ID3D11Device* device);
    ~ParticleComputeShader() override = default;

    // Shader基底のインターフェースを満たすための実装
    void Begin(ID3D11DeviceContext* dc, const RenderServices& s, const RenderFrameContext& ctx) override;
    void End(ID3D11DeviceContext* dc, const RenderServices& s) override;

    // 毎フレーム ShaderRegistry から呼ばれ、HLSLが更新されていたら自動再コンパイル
    void CheckAndReload(ID3D11Device* device) override;

    // システム側が実行(Dispatch)する際につまみ出すアクセサ
    ID3D11ComputeShader* GetInitCS() const noexcept { return _initCS.Get(); }
    ID3D11ComputeShader* GetUpdateCS() const noexcept { return _updateCS.Get(); }

private:
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> _initCS;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> _updateCS;

    std::wstring _initCsPath;
    std::wstring _updateCsPath;
    std::filesystem::file_time_type _initCsTime;
    std::filesystem::file_time_type _updateCsTime;

    void LoadShaders(ID3D11Device* device);
};