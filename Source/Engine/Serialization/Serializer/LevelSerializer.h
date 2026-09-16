// =====================================================================
// LevelSerializer.h
//   「レベル」= ステージ(ベイク済みシーン) + プレハブ実体(参照配置リスト)。
//
//   ・ステージ(床/背景)は SceneSerializer でベイク保存(自己完結・高速)。
//   ・プレイヤー/敵などのプレハブは {prefabPath, transform} の "参照" で保存し、
//     ロード時にプレハブから再生成する → プレハブ編集が全レベルへ反映される。
//
//   ファイル構成:
//     Foo.level.json  … このファイル(参照シーン名 + プレハブ配置リスト)
//     Foo.scene.json  … 同じ場所に書き出されるベイク済みステージ
// =====================================================================
#pragma once
#include "Engine/ECS/Core/CCL_World.h"
#include <string>

class LevelSerializer
{
public:
    // level ファイルと、その隣に <stem>.scene.json を書き出す。失敗時 false。
    static bool SaveLevel(CCL::ECS::World *world, const std::string &levelPath);

    // level を読み、参照シーンをロード(ワールド全消去→復元)後、
    // プレハブ実体を再生成して配置する。失敗時 false。
    static bool LoadLevel(CCL::ECS::World *world, const std::string &levelPath);
};
