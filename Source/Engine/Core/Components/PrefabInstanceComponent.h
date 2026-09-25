#pragma once
// ===========================================================================
// 概要: プレハブから参照配置した Entity のルートに付けるマーカー PrefabInstanceComponent。
//
// 責務:
//   - 元プレハブのパス（prefabPath）を保持する
//   - ステージ（ベイク）保存では、この Entity の子孫ごと SceneSerializer の対象から外す
//     （床/背景だけを焼く）
//   - レベル保存では {prefabPath, transform} の「参照」として書き出させる
//     （プレハブ側を編集すれば全レベルに反映される）
//
// 備考: ComponentRegistry には登録しない（＝シーン JSON には焼かれない）。
//       エディタ配置/レベル参照のためのランタイムマーカー。
// ===========================================================================
#include <string>
#include <utility>

struct PrefabInstanceComponent
{
    std::string prefabPath;

    PrefabInstanceComponent() = default;
    explicit PrefabInstanceComponent(std::string path) : prefabPath(std::move(path)) {}
};
