#pragma once
// ===========================================================================
// 概要: レベル（ステージ + プレハブの配置）を保存・読込するクラス LevelSerializer
//
// 責務:
//   - ステージ（床・背景など）は SceneSerializer で Entity を丸ごと保存する
//   - プレハブ（プレイヤー・敵など）はパスと Transform だけを保存し、読込時にプレハブから生成し直す
//
// 備考:
//   - プレハブを参照で持つので、プレハブを編集するとすべてのレベルに反映される
//   - ファイルは 2 つ: Foo.level.json（シーン名 + プレハブの配置）と、同じ場所の Foo.scene.json（ステージ）
// ===========================================================================
#include "Engine/ECS/Core/CCL_World.h"
#include <string>

class LevelSerializer
{
public:
    // level ファイルと、その隣に <stem>.scene.json を書き出す。失敗したら false
    static bool SaveLevel(CCL::ECS::World *world, const std::string &levelPath);

    // level を読み、シーンを読み込んで（World をすべて消して復元）からプレハブを生成して配置する。失敗したら false
    static bool LoadLevel(CCL::ECS::World *world, const std::string &levelPath);
};
