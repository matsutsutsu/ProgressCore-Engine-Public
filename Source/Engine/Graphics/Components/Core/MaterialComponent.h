#pragma once
// ===========================================================================
// 概要: モデルの共有マテリアルを、Entity ごとに差し替えるためのコンポーネント。
//
// 責務:
//   - メッシュのスロットごとに、差し替え先のマテリアルハンドルを持つ
//   - スロットごとに、シーン保存用の .mat.json パスを持つ
//   - シェーダーへそのまま渡す汎用パラメータ（customParams）を持つ
//
// 備考: ハンドルは実行時だけ有効なので保存できない。シーンには overridePaths を
//       保存し、ロード時にパスから LoadMaterialAsset で復元する（MaterialComponentMeta.cpp）。
//       スロットが空（無効ハンドル）なら、モデルの共有マテリアルで描く。
// ===========================================================================
#include <memory>
#include <string>
#include "Engine/Graphics/Resource/ResourceManager.h"

struct MaterialComponent
{
    MaterialComponent() = default;

    std::vector<MaterialHandle> overrideMaterials; // スロットごとの差し替えマテリアル。無効ハンドルなら共有マテリアルを使う

    // overrideMaterials と同じ長さで並ぶ .mat.json のパス。
    // パスを持つスロットだけがシーンに保存される。空文字は未保存の一時マテリアル。
    std::vector<std::string> overridePaths;

    DirectX::XMFLOAT4 customParams = {0.0f, 0.0f, 0.0f, 0.0f}; // インスタンスデータの customParams へそのまま渡す。意味はシェーダーごとに決める

    // index 番のスロットにハンドルを入れる。path が空でなければ、同じスロットのパスも更新する
    void SetData(size_t index, MaterialHandle sourceHandle, const std::string &path = "")
    {
        if (index >= overrideMaterials.size())
        {
            overrideMaterials.resize(index + 1, MaterialHandle{});
        }
        if (index >= overridePaths.size())
        {
            overridePaths.resize(index + 1);
        }
        overrideMaterials[index] = sourceHandle;
        if (!path.empty()) overridePaths[index] = path;
    }

    // 2つの配列を同じ長さに揃える（インスペクタとシリアライザから使う）
    void ResizeSlots(size_t count)
    {
        overrideMaterials.resize(count, MaterialHandle{});
        overridePaths.resize(count);
    }
};
