#pragma once
// ===========================================================================
// 概要: ShaderRegistry::RegisterMaterialShader で登録する、モデル描画用のシェーダー。
//
// 責務:
//   - 頂点・ピクセルシェーダーの .cso を読み、モデルの共通の入力レイアウトを作る
//   - メッシュのマテリアル（または上書きのマテリアル）を送る
//   - 頂点・ピクセルシェーダーの HLSL が更新されたら、コンパイルし直す
//
// 備考: ブレンド・深度・カリングは持たない。描くパス（OpaquePass / TransparentPass）が決める。
// ===========================================================================
#include "Engine/Graphics/Shader/Core/Shader.h"
#include <string>

class MaterialShader : public IMaterialShader
{
public:
    /**
     * @brief マテリアル用のシェーダーを作る。
     *
     * @param device     シェーダーを作るデバイス。
     * @param vsCsoPath  頂点シェーダーの cso。起動時に読む。
     * @param vsHlslPath 頂点シェーダーの hlsl。ホットリロードで監視してコンパイルし直す。
     * @param psCsoPath  ピクセルシェーダーの cso。起動時に読む。
     * @param psHlslPath ピクセルシェーダーの hlsl。ホットリロードで監視してコンパイルし直す。
     */
    MaterialShader(ID3D11Device      *device,
                   const std::string &vsCsoPath, const std::wstring &vsHlslPath,
                   const std::string &psCsoPath, const std::wstring &psHlslPath);
    ~MaterialShader() override = default;

    void Begin(ID3D11DeviceContext *dc, const RenderServices &s, const RenderFrameContext &ctx) override;
    // 上書きのマテリアルがあればそれを送る。テクスチャが無いスロットはメッシュのマテリアルで補う
    void Update(ID3D11DeviceContext *dc, const Model::Mesh &mesh,
                const MaterialData *overrideMaterial = nullptr) override;
    void End(ID3D11DeviceContext *dc, const RenderServices &s) override;

    // ピクセルシェーダーの HLSL が更新されていたらコンパイルし直す（頂点シェーダーは見ていない）
    void CheckAndReload(ID3D11Device *device) override;

private:
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>  inputLayout;

    std::wstring                    m_vsHlslPath;
    std::wstring                    m_psHlslPath;
    std::filesystem::file_time_type m_vsLastWriteTime; // 前に見た HLSL の更新時刻
    std::filesystem::file_time_type m_psLastWriteTime;
};