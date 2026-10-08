#pragma once
// ===========================================================================
// 概要: シェーダー・テクスチャ・定数バッファを作るための static 関数の集まり。
//
// 責務:
//   - コンパイル済みの .cso からシェーダーを作る（Load 系）
//   - HLSL をその場でコンパイルしてシェーダーを作る（Compile 系。ホットリロード用）
//   - 画像ファイル・メモリからテクスチャを読み込み、単色のダミーテクスチャを作る
//   - 定数バッファ・頂点バッファ・インデックスバッファを作る
//
// 備考: HLSL の #include は、プロジェクトのルート・Source/・Assets/ の順に探す。
// ===========================================================================
#include <d3d11.h>
#include <string>
#include <d3dcompiler.h>

class GpuResourceUtils
{
public:
    // ============================================================
    // Load 系（コンパイル済みの .cso を読む）
    // ============================================================

    // 頂点シェーダーと、その入力レイアウトを作る
    static HRESULT LoadVertexShader(
        ID3D11Device                  *device,
        const char                    *filename,
        const D3D11_INPUT_ELEMENT_DESC inputElementDescs[],
        UINT                           inputElementCount,
        ID3D11InputLayout            **inputLayout,
        ID3D11VertexShader           **vertexShader);

    static HRESULT LoadPixelShader(
        ID3D11Device       *device,
        const char         *filename,
        ID3D11PixelShader **pixelShader);

    static HRESULT LoadGeometryShader(
        ID3D11Device          *device,
        const char            *filename,
        ID3D11GeometryShader **geometryShader);

    // コンパイル済みの .cso からコンピュートシェーダーを作る。HLSL を同梱しない Shipping ビルドでも動く
    static HRESULT LoadComputeShader(ID3D11Device         *device,
                                     const char           *filename,
                                     ID3D11ComputeShader **computeShader);

    // ============================================================
    // テクスチャ・バッファ
    // ============================================================

    // 画像ファイル（.dds / .tga / .hdr / WIC 対応形式）からテクスチャを読む
    static HRESULT LoadTexture(
        ID3D11Device              *device,
        const char                *filename,
        ID3D11ShaderResourceView **shaderResourceView,
        D3D11_TEXTURE2D_DESC      *texture2dDesc = nullptr);

    // メモリ上の画像データからテクスチャを読む（glTF に埋め込まれた画像など）
    static HRESULT LoadTexture(
        ID3D11Device              *device,
        const void                *data,
        size_t                     size,
        ID3D11ShaderResourceView **shaderResourceView,
        D3D11_TEXTURE2D_DESC      *texture2dDesc = nullptr);

    // color（0xAABBGGRR）一色のテクスチャを作る
    static HRESULT CreateDummyTexture(
        ID3D11Device              *device,
        UINT                       color,
        ID3D11ShaderResourceView **shaderResourceView,
        D3D11_TEXTURE2D_DESC      *texture2dDesc = nullptr);

    // UpdateSubresource で書き込む（DEFAULT）定数バッファを作る
    static HRESULT CreateConstantBuffer(
        ID3D11Device  *device,
        UINT           bufferSize,
        ID3D11Buffer **constantBuffer);

    // CPU から Map で書き込む（DYNAMIC）定数バッファを作る
    static HRESULT CreateDynamicConstantBuffer(
        ID3D11Device  *device,
        UINT           bufferSize,
        ID3D11Buffer **constantBuffer);

    // 書き換えない（DEFAULT）頂点バッファを data で初期化して作る。size はバイト数
    static HRESULT CreateVertexBuffer(
        ID3D11Device  *device,
        const void    *data,
        UINT           size,
        ID3D11Buffer **buffer);

    // 書き換えない（DEFAULT）32bit インデックスバッファを data で初期化して作る。size はバイト数
    static HRESULT CreateIndexBuffer(
        ID3D11Device  *device,
        const UINT    *data,
        UINT           size,
        ID3D11Buffer **buffer);

    // ============================================================
    // Compile 系（HLSL をその場でコンパイルする）
    // ============================================================

    // 頂点シェーダーを作る。inputLayout と inputElementDescs を渡したときだけ入力レイアウトも作る
    static bool CompileVertexShader(ID3D11Device                   *device,
                                    const std::wstring             &hlslPath,
                                    const char                     *entryPoint,
                                    const char                     *shaderModel,
                                    ID3D11VertexShader            **vertexShader,
                                    ID3D11InputLayout             **inputLayout       = nullptr,
                                    const D3D11_INPUT_ELEMENT_DESC *inputElementDescs = nullptr,
                                    UINT                            inputElementCount = 0,
                                    ID3DBlob                      **outShaderBlob     = nullptr);

    static bool CompilePixelShader(ID3D11Device       *device,
                                   const std::wstring &hlslPath,
                                   const char         *entryPoint,
                                   const char         *shaderModel,
                                   ID3D11PixelShader **pixelShader,
                                   ID3DBlob          **outShaderBlob = nullptr);

    static bool CompileGeometryShader(ID3D11Device          *device,
                                      const std::wstring    &hlslPath,
                                      const char            *entryPoint,
                                      const char            *shaderModel,
                                      ID3D11GeometryShader **geometryShader,
                                      ID3DBlob             **outShaderBlob = nullptr);

    // コンピュートシェーダーを .hlsl からコンパイルして作る
    static bool CompileComputeShader(ID3D11Device         *device,
                                     const std::wstring   &hlslPath,
                                     const char           *entryPoint,
                                     const char           *shaderModel,
                                     ID3D11ComputeShader **computeShader,
                                     ID3DBlob            **outShaderBlob = nullptr);
};
