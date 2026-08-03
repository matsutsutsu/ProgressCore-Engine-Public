#pragma once
#include "Engine/Graphics/Shader/Core/Shader.h"
#include "Engine/Graphics/RHI/PipelineState.h"  
#include <string>

// すべてのモデル描画用シェーダーを統合する汎用クラス
class MaterialShader : public IMaterialShader {
public:
    // CSOファイルのパスを受け取るコンストラクタ
	// ホットリロード対応でhlslファイルのパスも受け取るように拡張
    MaterialShader(ID3D11Device* device,
        const std::string& vsCsoPath, const std::wstring& vsHlslPath,
        const std::string& psCsoPath, const std::wstring& psHlslPath,
        const PipelineStateDesc* psoDesc = nullptr); 
    ~MaterialShader() override = default;

    void Begin(ID3D11DeviceContext* dc, const RenderServices& s, const RenderFrameContext& ctx) override;
    /**
    * @brief overrideMaterial が非 null の場合はそちらを優先してプロパティを転送する。
    *        mesh.material を一切書き換えない。
    */
    void Update(ID3D11DeviceContext* dc, const Model::Mesh& mesh,
        const MaterialData* overrideMaterial = nullptr) override;
    void End(ID3D11DeviceContext* dc, const RenderServices& s) override;

     // JSON 由来の PSO を持っていれば返す。無ければ nullptr（パス既定を使わせる）。
    const PipelineState* GetPSO() const override { return m_hasPso ? &m_pso : nullptr; }

    // ファイルの更新を監視し、再コンパイルを実行する
    void CheckAndReload(ID3D11Device* device) override;

private:
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>  inputLayout;

    // シェーダー固有 PSO（m_hasPso が false なら未設定）
    PipelineState m_pso;
    bool          m_hasPso = false;

    std::wstring m_vsHlslPath;
    std::wstring m_psHlslPath;
    std::filesystem::file_time_type m_vsLastWriteTime;
    std::filesystem::file_time_type m_psLastWriteTime;


};