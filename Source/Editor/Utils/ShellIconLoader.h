#pragma once
// ===========================================================================
// 概要: エクスプローラと同じ OS 標準のアイコンを取得し、D3D11 のテクスチャにするクラス。
//
// 責務:
//   - ファイル・フォルダのアイコンを SHGetFileInfo で取得し、SRV に変換する
//   - ファイルは拡張子ごと、フォルダはパスごとにキャッシュする
//
// 備考: キャッシュは D3D11 のリソースを持つので、デバイスの破棄前に ClearCache を呼ぶ。
// ===========================================================================
#include <string>
#include <unordered_map>
#include <d3d11.h>
#include <wrl/client.h>

class ShellIconLoader
{
public:
    // ファイル・フォルダどちらのパスも渡せる。取得に失敗したら nullptr
    static Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> GetIconSRV(
        const std::wstring &filePath, ID3D11Device *device);

    // デバイスの破棄前に必ず呼ぶ（AssetBrowser のデストラクタが呼んでいる）
    static void ClearCache();

private:
    static Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> LoadShellIconSRV(
        const std::wstring &filePath, ID3D11Device *device);

    static Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> LoadFolderIconSRV(
        const std::wstring &folderPath, ID3D11Device *device);

    static Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> CreateSRVFromHICON(
        ID3D11Device *device, HICON hIcon);

    static std::unordered_map<std::wstring, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> cache;       // 拡張子（小文字）→ アイコン
    static std::unordered_map<std::wstring, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> folderCache; // フォルダのパス → アイコン
};
