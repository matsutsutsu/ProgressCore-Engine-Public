#pragma once
#include "Engine/Graphics/Shader/Core/Shader.h"
#include <string>

// すべてのモデル描画用シェーダーを統合する汎用クラス
class MaterialShader : public Shader {
public:
    // CSOファイルのパスを受け取るコンストラクタ
	// ホットリロード対応でhlslファイルのパスも受け取るように拡張
    MaterialShader(ID3D11Device* device,
        const std::string& vsCsoPath, const std::wstring& vsHlslPath,
        const std::string& psCsoPath, const std::wstring& psHlslPath);
    ~MaterialShader() override = default;

    void Begin(ID3D11DeviceContext* dc, const RenderServices& s, const RenderFrameContext& ctx) override;
    void Update(ID3D11DeviceContext* dc, const Model::Mesh& mesh) override;
    void End(ID3D11DeviceContext* dc, const RenderServices& s) override;

    // ファイルの更新を監視し、再コンパイルを実行する
    void CheckAndReload(ID3D11Device* device) override;

private:
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>  inputLayout;

    std::wstring _vsHlslPath;
    std::wstring _psHlslPath;
    std::filesystem::file_time_type _vsLastWriteTime;
    std::filesystem::file_time_type _psLastWriteTime;

    // ヘルパー関数: ファイルの最終更新日時を取得
    std::filesystem::file_time_type GetLastWriteTime(const std::wstring& path);
};