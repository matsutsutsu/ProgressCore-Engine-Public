#include <filesystem>
#include <fstream>
#include <wrl.h>
#include <DirectXTex.h>
#include "Engine/Core/Math/Misc.h"
#include "GpuResourceUtils.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Graphics/RHI/DebugName.h"
#include <windows.h>

#pragma comment(lib, "d3dcompiler.lib")

// ---------------------------------------------------------------------------
// HLSL の #include を、プロジェクトのルート・Source/・Assets/ の順に探すハンドラ
// ---------------------------------------------------------------------------
class RootInclude : public ID3DInclude
{
public:
    HRESULT __stdcall Open(D3D_INCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData, LPCVOID *ppData, UINT *pBytes) override
    {
        std::vector<std::string> searchPaths = {
            pFileName,
            std::string("Source/") + pFileName,
            std::string("Assets/") + pFileName};

        std::ifstream file;
        for (const auto &path : searchPaths)
        {
            file.open(path, std::ios::binary | std::ios::ate);
            if (file.is_open()) break;
        }

        if (!file.is_open())
        {
            char currentDir[MAX_PATH];
            GetCurrentDirectoryA(MAX_PATH, currentDir);

            std::string pathsInfo;
            for (const auto &path : searchPaths)
            {
                pathsInfo += "  - " + path + "\n";
            }

            CCL_LOG_ERROR(LogCategory::Render, "HLSL Include Failed!\nRequested File: %s\nCurrent Dir: %s\nChecked Paths:\n%s",
                          pFileName, currentDir, pathsInfo.c_str());

            return E_FAIL;
        }

        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);
        char *buf = new char[size];
        file.read(buf, size);
        file.close();

        *ppData = buf;
        *pBytes = static_cast<UINT>(size);
        return S_OK;
    }

    HRESULT __stdcall Close(LPCVOID pData) override
    {
        delete[] static_cast<const char *>(pData);
        return S_OK;
    }
};

// ---------------------------------------------------------------------------
// シェーダーの読み込み（.cso）
// ---------------------------------------------------------------------------

HRESULT GpuResourceUtils::LoadVertexShader(
    ID3D11Device                  *device,
    const char                    *filename,
    const D3D11_INPUT_ELEMENT_DESC inputElementDescs[],
    UINT                           inputElementCount,
    ID3D11InputLayout            **inputLayout,
    ID3D11VertexShader           **vertexShader)
{
    // ファイルを開く
    FILE *fp = nullptr;
    fopen_s(&fp, filename, "rb");
    _ASSERT_EXPR_A(fp, "Vertex Shader File not found");

    // ファイルのサイズを求める
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // .cso の中身を読む
    std::unique_ptr<uint8_t[]> data = std::make_unique<uint8_t[]>(size);
    fread(data.get(), size, 1, fp);
    fclose(fp);

    // 頂点シェーダー生成
    HRESULT hr = device->CreateVertexShader(data.get(), size, nullptr, vertexShader);
    _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

    // 入力レイアウト
    if (inputLayout != nullptr)
    {
        hr = device->CreateInputLayout(inputElementDescs, inputElementCount, data.get(), size, inputLayout);
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
    }

    return hr;
}

HRESULT GpuResourceUtils::LoadPixelShader(
    ID3D11Device       *device,
    const char         *filename,
    ID3D11PixelShader **pixelShader)
{
    // ファイルを開く
    FILE *fp = nullptr;
    fopen_s(&fp, filename, "rb");
    _ASSERT_EXPR_A(fp, "Pixel Shader File not found");

    // ファイルのサイズを求める
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // .cso の中身を読む
    std::unique_ptr<uint8_t[]> data = std::make_unique<uint8_t[]>(size);
    fread(data.get(), size, 1, fp);
    fclose(fp);

    // ピクセルシェーダー生成
    HRESULT hr = device->CreatePixelShader(data.get(), size, nullptr, pixelShader);
    _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

    return hr;
}

HRESULT GpuResourceUtils::LoadGeometryShader(
    ID3D11Device          *device,
    const char            *filename,
    ID3D11GeometryShader **geometryShader)
{
    FILE *fp = nullptr;
    fopen_s(&fp, filename, "rb");
    _ASSERT_EXPR_A(fp, "Geometry Shader File not found");

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    std::unique_ptr<uint8_t[]> data = std::make_unique<uint8_t[]>(size);
    fread(data.get(), size, 1, fp);
    fclose(fp);

    HRESULT hr = device->CreateGeometryShader(data.get(), size, nullptr, geometryShader);
    _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

    return hr;
}

// ---------------------------------------------------------------------------
// テクスチャ
// ---------------------------------------------------------------------------

HRESULT GpuResourceUtils::LoadTexture(
    ID3D11Device              *device,
    const char                *filename,
    ID3D11ShaderResourceView **shaderResourceView,
    D3D11_TEXTURE2D_DESC      *texture2dDesc)
{
    // 拡張子を取得
    std::filesystem::path filepath(filename);
    std::string           extension = filepath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), tolower); // 小文字化

    // ワイド文字に変換
    std::wstring wfilename = filepath.wstring();

    // フォーマット毎に画像読み込み処理
    HRESULT               hr;
    DirectX::TexMetadata  metadata;
    DirectX::ScratchImage scratch_image;
    if (extension == ".tga")
    {
        hr = DirectX::GetMetadataFromTGAFile(wfilename.c_str(), metadata);
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

        hr = DirectX::LoadFromTGAFile(wfilename.c_str(), &metadata, scratch_image);
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
    }
    else if (extension == ".dds")
    {
        hr = DirectX::GetMetadataFromDDSFile(wfilename.c_str(), DirectX::DDS_FLAGS_NONE, metadata);
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

        hr = DirectX::LoadFromDDSFile(wfilename.c_str(), DirectX::DDS_FLAGS_NONE, &metadata, scratch_image);
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
    }
    else if (extension == ".hdr")
    {
        hr = DirectX::GetMetadataFromHDRFile(wfilename.c_str(), metadata);
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

        hr = DirectX::LoadFromHDRFile(wfilename.c_str(), &metadata, scratch_image);
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
    }
    else
    {
        hr = DirectX::GetMetadataFromWICFile(wfilename.c_str(), DirectX::WIC_FLAGS_NONE, metadata);
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

        hr = DirectX::LoadFromWICFile(wfilename.c_str(), DirectX::WIC_FLAGS_NONE, &metadata, scratch_image);
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
    }

    // シェーダーリソースビュー作成
    hr = DirectX::CreateShaderResourceView(device, scratch_image.GetImages(), scratch_image.GetImageCount(),
                                           metadata, shaderResourceView);
    _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

    // デバッグ名設定
    SetDebugName(*shaderResourceView, "Texture_SRV");

    // テクスチャ情報取得
    if (texture2dDesc != nullptr)
    {
        Microsoft::WRL::ComPtr<ID3D11Resource> resource;
        (*shaderResourceView)->GetResource(resource.GetAddressOf());

        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture2d;
        hr = resource->QueryInterface<ID3D11Texture2D>(texture2d.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
        texture2d->GetDesc(texture2dDesc);
    }
    return hr;
}

HRESULT GpuResourceUtils::LoadTexture(
    ID3D11Device              *device,
    const void                *data,
    size_t                     size,
    ID3D11ShaderResourceView **shaderResourceView,
    D3D11_TEXTURE2D_DESC      *texture2dDesc)
{
    // フォーマット毎に画像読み込み処理
    HRESULT               hr = E_FAIL;
    DirectX::TexMetadata  metadata;
    DirectX::ScratchImage scratch_image;

    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(data);

    // .tga
    {
        hr = DirectX::GetMetadataFromTGAMemory(bytes, size, metadata);
        if (SUCCEEDED(hr))
        {
            hr = DirectX::LoadFromTGAMemory(bytes, size, &metadata, scratch_image);
        }
    }

    // .dds
    if (FAILED(hr))
    {
        hr = DirectX::GetMetadataFromDDSMemory(bytes, size, DirectX::DDS_FLAGS_NONE, metadata);
        if (SUCCEEDED(hr))
        {
            hr = DirectX::LoadFromDDSMemory(
                bytes, size, DirectX::DDS_FLAGS_NONE, &metadata, scratch_image);
        }
    }

    // .hdr
    if (FAILED(hr))
    {
        hr = DirectX::GetMetadataFromHDRMemory(bytes, size, metadata);
        if (SUCCEEDED(hr))
        {
            hr = DirectX::LoadFromHDRMemory(bytes, size, &metadata, scratch_image);
        }
    }

    // WIC
    if (FAILED(hr))
    {
        hr = DirectX::GetMetadataFromWICMemory(bytes, size, DirectX::WIC_FLAGS_NONE, metadata);
        if (SUCCEEDED(hr))
        {
            hr = DirectX::LoadFromWICMemory(
                bytes, size, DirectX::WIC_FLAGS_NONE, &metadata, scratch_image);
        }
    }

    // シェーダーリソースビュー作成
    hr = DirectX::CreateShaderResourceView(device, scratch_image.GetImages(), scratch_image.GetImageCount(),
                                           metadata, shaderResourceView);

    // デバッグ名設定
    SetDebugName(*shaderResourceView, "Texture_SRV");

    _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

    // テクスチャ情報取得
    if (texture2dDesc != nullptr)
    {
        Microsoft::WRL::ComPtr<ID3D11Resource> resource;
        (*shaderResourceView)->GetResource(resource.GetAddressOf());

        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture2d;
        hr = resource->QueryInterface<ID3D11Texture2D>(texture2d.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
        texture2d->GetDesc(texture2dDesc);
    }
    return hr;
}

HRESULT GpuResourceUtils::CreateDummyTexture(
    ID3D11Device              *device,
    UINT                       color,
    ID3D11ShaderResourceView **shaderResourceView,
    D3D11_TEXTURE2D_DESC      *texture2dDesc)
{
    D3D11_TEXTURE2D_DESC desc = {0};
    desc.Width                = 1;
    desc.Height               = 1;
    desc.MipLevels            = 1;
    desc.ArraySize            = 1;
    desc.Format               = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count     = 1;
    desc.SampleDesc.Quality   = 0;
    desc.Usage                = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags            = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags       = 0;
    desc.MiscFlags            = 0;
    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem     = &color;
    data.SysMemPitch = desc.Width;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    HRESULT                                 hr = device->CreateTexture2D(&desc, &data, texture.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

    hr = device->CreateShaderResourceView(texture.Get(), nullptr, shaderResourceView);
    _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

    // デバッグ名設定
    SetDebugName(*shaderResourceView, "DummyTexture_SRV");

    // テクスチャ情報取得
    if (texture2dDesc != nullptr)
    {
        Microsoft::WRL::ComPtr<ID3D11Resource> resource;
        (*shaderResourceView)->GetResource(resource.GetAddressOf());

        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture2d;
        hr = resource->QueryInterface<ID3D11Texture2D>(texture2d.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
        texture2d->GetDesc(texture2dDesc);
    }

    return hr;
}

// ---------------------------------------------------------------------------
// バッファ
// ---------------------------------------------------------------------------

HRESULT GpuResourceUtils::CreateConstantBuffer(ID3D11Device *device, UINT bufferSize, ID3D11Buffer **constantBuffer)
{
    D3D11_BUFFER_DESC desc{};
    desc.Usage               = D3D11_USAGE_DEFAULT;
    desc.BindFlags           = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags      = 0;
    desc.MiscFlags           = 0;
    desc.ByteWidth           = bufferSize;
    desc.StructureByteStride = 0;

    HRESULT hr = device->CreateBuffer(&desc, 0, constantBuffer);
    _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

    return hr;
}

HRESULT GpuResourceUtils::CreateDynamicConstantBuffer(ID3D11Device *device, UINT bufferSize, ID3D11Buffer **constantBuffer)
{
    D3D11_BUFFER_DESC desc{};
    desc.Usage     = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    // DYNAMIC では D3D11_CPU_ACCESS_WRITE が必須
    desc.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;
    desc.MiscFlags           = 0;
    desc.ByteWidth           = bufferSize;
    desc.StructureByteStride = 0;

    HRESULT hr = device->CreateBuffer(&desc, 0, constantBuffer);
    _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

    return hr;
}

HRESULT GpuResourceUtils::CreateVertexBuffer(ID3D11Device *device, const void *data, UINT size, ID3D11Buffer **buffer)
{
    D3D11_BUFFER_DESC desc{};
    desc.Usage          = D3D11_USAGE_DEFAULT;
    desc.ByteWidth      = size;
    desc.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
    desc.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA subData{};
    subData.pSysMem = data;

    return device->CreateBuffer(&desc, &subData, buffer);
}

HRESULT GpuResourceUtils::CreateIndexBuffer(ID3D11Device *device, const UINT *data, UINT size, ID3D11Buffer **buffer)
{
    D3D11_BUFFER_DESC desc{};
    desc.Usage          = D3D11_USAGE_DEFAULT;
    desc.ByteWidth      = size;
    desc.BindFlags      = D3D11_BIND_INDEX_BUFFER;
    desc.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA subData{};
    subData.pSysMem = data;

    return device->CreateBuffer(&desc, &subData, buffer);
}

// ---------------------------------------------------------------------------
// シェーダーのコンパイル（.hlsl）
// ---------------------------------------------------------------------------

bool GpuResourceUtils::CompileVertexShader(ID3D11Device                   *device,
                                           const std::wstring             &hlslPath,
                                           const char                     *entryPoint,
                                           const char                     *shaderModel,
                                           ID3D11VertexShader            **vertexShader,
                                           ID3D11InputLayout             **inputLayout,
                                           const D3D11_INPUT_ELEMENT_DESC *inputElementDescs,
                                           UINT                            inputElementCount,
                                           ID3DBlob                      **outShaderBlob)
{
    Microsoft::WRL::ComPtr<ID3DBlob> shaderBlob = nullptr;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob  = nullptr;
    RootInclude                      rootInclude;

    HRESULT hr = D3DCompileFromFile(hlslPath.c_str(), nullptr, &rootInclude,
                                    entryPoint, shaderModel, D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_DEBUG, 0, &shaderBlob, &errorBlob);

    if (FAILED(hr))
    {
        if (errorBlob) OutputDebugStringA((char *)errorBlob->GetBufferPointer());
        return false;
    }

    hr = device->CreateVertexShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, vertexShader);
    if (FAILED(hr)) return false;

    if (inputLayout && inputElementDescs && inputElementCount > 0)
    {
        hr = device->CreateInputLayout(inputElementDescs, inputElementCount,
                                       shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), inputLayout);
        if (FAILED(hr)) return false;
    }

    if (outShaderBlob) *outShaderBlob = shaderBlob.Detach();
    return true;
}

bool GpuResourceUtils::CompileGeometryShader(ID3D11Device          *device,
                                             const std::wstring    &hlslPath,
                                             const char            *entryPoint,
                                             const char            *shaderModel,
                                             ID3D11GeometryShader **geometryShader,
                                             ID3DBlob             **outShaderBlob)
{
    Microsoft::WRL::ComPtr<ID3DBlob> shaderBlob = nullptr;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob  = nullptr;
    RootInclude                      rootInclude;

    HRESULT hr = D3DCompileFromFile(hlslPath.c_str(), nullptr, &rootInclude,
                                    entryPoint, shaderModel, D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_DEBUG, 0, &shaderBlob, &errorBlob);

    if (FAILED(hr))
    {
        if (errorBlob) OutputDebugStringA((char *)errorBlob->GetBufferPointer());
        return false;
    }

    hr = device->CreateGeometryShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, geometryShader);
    if (FAILED(hr)) return false;

    if (outShaderBlob) *outShaderBlob = shaderBlob.Detach();
    return true;
}

bool GpuResourceUtils::CompilePixelShader(ID3D11Device       *device,
                                          const std::wstring &hlslPath,
                                          const char         *entryPoint,
                                          const char         *shaderModel,
                                          ID3D11PixelShader **pixelShader,
                                          ID3DBlob          **outShaderBlob)
{
    Microsoft::WRL::ComPtr<ID3DBlob> shaderBlob = nullptr;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob  = nullptr;

    // D3D_COMPILE_STANDARD_FILE_INCLUDE はファイルの場所から相対で探すので、
    // プロジェクトのルートから探す RootInclude を渡す（hlslPath は相対パスのままでよい）
    RootInclude rootInclude;

    HRESULT hr = D3DCompileFromFile(hlslPath.c_str(),
                                    nullptr,
                                    &rootInclude,
                                    entryPoint,
                                    shaderModel,
                                    D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_DEBUG,
                                    0,
                                    &shaderBlob,
                                    &errorBlob);

    if (FAILED(hr))
    {
        if (errorBlob)
        {
            OutputDebugStringA("\n[Shader Compile Error]\n");
            OutputDebugStringA((char *)errorBlob->GetBufferPointer());
        }
        else
        {
            // エラーの内容が無いのは、ファイルを開けなかったとき
            OutputDebugStringA("\n[Shader Compile Error] File not found or cannot be opened: ");
            OutputDebugStringW(hlslPath.c_str());
            OutputDebugStringA("\n");
        }
        return false;
    }

    hr = device->CreatePixelShader(
        shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, pixelShader);
    if (FAILED(hr)) return false;

    if (outShaderBlob)
    {
        *outShaderBlob = shaderBlob.Detach();
    }

    return true;
}

bool GpuResourceUtils::CompileComputeShader(ID3D11Device       *device,
                                            const std::wstring &hlslPath, const char *entryPoint, const char *shaderModel,
                                            ID3D11ComputeShader **computeShader, ID3DBlob **outShaderBlob)
{
    Microsoft::WRL::ComPtr<ID3DBlob> shaderBlob = nullptr;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob  = nullptr;
    RootInclude                      rootInclude;

    HRESULT hr = D3DCompileFromFile(hlslPath.c_str(), nullptr, &rootInclude,
                                    entryPoint, shaderModel, D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_DEBUG, 0, &shaderBlob, &errorBlob);

    if (FAILED(hr))
    {
        if (errorBlob)
        {
            // %ls はワイド文字列の書式指定
            CCL_LOG_ERROR(LogCategory::Render, "Compute Shader Compile Error [%ls]\n%s",
                          hlslPath.c_str(), (char *)errorBlob->GetBufferPointer());
        }
        else
        {
            // エラーの内容が無いのは、ファイルを開けなかったとき
            CCL_LOG_ERROR(LogCategory::Render, "Compute Shader File not found or cannot be opened: %ls", hlslPath.c_str());
        }
        return false;
    }

    hr = device->CreateComputeShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, computeShader);
    if (FAILED(hr)) return false;

    if (outShaderBlob) *outShaderBlob = shaderBlob.Detach();

    return true;
}

HRESULT GpuResourceUtils::LoadComputeShader(
    ID3D11Device         *device,
    const char           *filename,
    ID3D11ComputeShader **computeShader)
{
    // ファイルを開く
    FILE *fp = nullptr;
    fopen_s(&fp, filename, "rb");
    if (!fp)
    {
        CCL_LOG_ERROR(LogCategory::Render, "Compute Shader (.cso) not found: %s", filename);
        return E_FAIL;
    }

    // ファイルのサイズを求める
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // メモリ上にバイナリを格納
    std::unique_ptr<uint8_t[]> data = std::make_unique<uint8_t[]>(size);
    fread(data.get(), size, 1, fp);
    fclose(fp);

    // コンピュートシェーダー生成
    HRESULT hr = device->CreateComputeShader(data.get(), size, nullptr, computeShader);
    _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

    return hr;
}
