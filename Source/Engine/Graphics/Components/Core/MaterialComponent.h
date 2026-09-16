#pragma once
#include <memory>
#include <string>
#include "Engine/Graphics/Resource/ResourceManager.h" // ハンドルを使うために追加

struct MaterialComponent
{
    MaterialComponent() = default;

    // shared_ptrを完全に排除し、8バイトのチケットの配列にする
    std::vector<MaterialHandle> overrideMaterials;

    // overrideMaterials と 1:1 で並走する「アセット参照パス」列。
    //   .mat.json のパスを持つスロットだけがシーン保存で永続化される
    //   （A案＝アセット参照方式）。空文字＝ファイル未保存の一時マテリアル。
    //   シーンロード時はこのパスから遅延解決して overrideMaterials を復元する。
    std::vector<std::string> overridePaths;

    // シェーダーの instanceData.customParams にそのまま直結するデータ
    DirectX::XMFLOAT4 customParams = {0.0f, 0.0f, 0.0f, 0.0f};

    // ハンドル（チケット）を受け取ってセットする。
    //   path を渡すと同スロットのアセット参照パスも更新する（空なら据え置き）。
    void SetData(size_t index, MaterialHandle sourceHandle, const std::string &path = "")
    {
        if (index >= overrideMaterials.size())
        {
            // 足りない分は「無効なチケット(中身が空のHandle)」で埋める
            overrideMaterials.resize(index + 1, MaterialHandle{});
        }
        if (index >= overridePaths.size())
        {
            overridePaths.resize(index + 1);
        }
        // チケットをコピーするだけなので処理速度は実質ゼロ
        overrideMaterials[index] = sourceHandle;
        if (!path.empty()) overridePaths[index] = path;
    }

    // 2つの配列を常に同じ長さに保つためのヘルパ（GUI/シリアライザ双方から使う）。
    void ResizeSlots(size_t count)
    {
        overrideMaterials.resize(count, MaterialHandle{});
        overridePaths.resize(count);
    }
};
