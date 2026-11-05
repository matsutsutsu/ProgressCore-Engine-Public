#pragma once
#include <string>
#include <utility>

// プレハブから参照で配置した Entity のルートに付ける目印。ComponentRegistry には登録せず、シーン JSON に焼かない
// レベル保存では {prefabPath, transform} の参照として書き出し、ステージ保存では子孫ごと外す
struct PrefabInstanceComponent
{
    std::string prefabPath;

    PrefabInstanceComponent() = default;
    explicit PrefabInstanceComponent(std::string path) : prefabPath(std::move(path)) {}
};
