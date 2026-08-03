#include "ShellIconLoader.h"
#include <Windows.h>
#include <shellapi.h>
#include <shlobj_core.h>
#include <vector>
#include <algorithm>
#include <filesystem>
#include "Engine/Graphics/RHI/Graphics.h"

using namespace Microsoft::WRL;
namespace fs = std::filesystem;

std::unordered_map<std::wstring, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> ShellIconLoader::cache;
std::unordered_map<std::wstring, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> ShellIconLoader::folderCache;

void ShellIconLoader::ClearCache()
{
    cache.clear();
    folderCache.clear();
}

Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ShellIconLoader::GetIconSRV(
    const std::wstring &filePath, ID3D11Device *device)
{
    if (std::filesystem::is_directory(filePath))
    {
        if (folderCache.count(filePath))
            return folderCache[filePath];

        auto srv              = LoadFolderIconSRV(filePath, device);
        folderCache[filePath] = srv;
        return srv;
    }

    // AAA品質の最適化: 実ファイルを解析せず、「拡張子」をキーにしてキャッシュを回す
    std::wstring ext = std::filesystem::path(filePath).extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);

    if (cache.count(ext))
        return cache[ext];

    auto srv   = LoadShellIconSRV(filePath, device);
    cache[ext] = srv; // 同じ拡張子のファイルは、二度とOSに問い合わせない
    return srv;
}

Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ShellIconLoader::LoadFolderIconSRV(
    const std::wstring &folderPath, ID3D11Device *device)
{
    SHFILEINFOW sfi = {};
    if (SHGetFileInfoW(folderPath.c_str(), FILE_ATTRIBUTE_DIRECTORY, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES))
    {
        HICON hIcon = sfi.hIcon;
        auto  srv   = CreateSRVFromHICON(device, hIcon);
        DestroyIcon(hIcon);
        return srv;
    }
    return nullptr;
}

Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ShellIconLoader::LoadShellIconSRV(
    const std::wstring &filePath, ID3D11Device *device)
{
    SHFILEINFOW sfi = {};
    // 魔法のフラグ: SHGFI_USEFILEATTRIBUTES
    // 実ファイルの存在や中身を無視し、「この拡張子ならOS上ではどのアイコンになるか」だけを爆速で取得する
    if (SHGetFileInfoW(filePath.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES))
    {
        HICON hIcon = sfi.hIcon;
        auto  srv   = CreateSRVFromHICON(device, hIcon);
        DestroyIcon(hIcon);
        return srv;
    }
    return nullptr;
}

Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ShellIconLoader::CreateSRVFromHICON(
    ID3D11Device *device, HICON hIcon)
{
    if (!hIcon) return nullptr;

    ICONINFO iconInfo;
    if (!GetIconInfo(hIcon, &iconInfo)) return nullptr;

    BITMAP bmp;
    if (!GetObject(iconInfo.hbmColor, sizeof(BITMAP), &bmp))
    {
        DeleteObject(iconInfo.hbmColor);
        DeleteObject(iconInfo.hbmMask);
        return nullptr;
    }

    int                        width  = bmp.bmWidth;
    int                        height = bmp.bmHeight;
    int                        pitch  = width * 4;
    std::vector<unsigned char> pixels(pitch * height, 0);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = width;
    bmi.bmiHeader.biHeight      = -height; // 上下反転防止
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdc = CreateCompatibleDC(NULL);
    GetDIBits(hdc, iconInfo.hbmColor, 0, height, pixels.data(), &bmi, DIB_RGB_COLORS);

    DeleteDC(hdc);
    DeleteObject(iconInfo.hbmColor);
    DeleteObject(iconInfo.hbmMask);

    D3D11_TEXTURE2D_DESC texDesc{};
    texDesc.Width            = width;
    texDesc.Height           = height;
    texDesc.MipLevels        = 1;
    texDesc.ArraySize        = 1;
    texDesc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage            = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData{};
    initData.pSysMem     = pixels.data();
    initData.SysMemPitch = pitch;

    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device->CreateTexture2D(&texDesc, &initData, &texture))) return nullptr;

    ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, &srv))) return nullptr;

    return srv;
}