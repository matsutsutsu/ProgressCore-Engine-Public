#pragma once
// ===========================================================================
// 概要: アニメーションのステートグラフ（AnimStateGraph）を JSON ファイルへ保存・読込するクラス。
//
// 責務:
//   - AnimStateGraph と JSON ファイルの相互変換
//
// 備考: 読み込んだグラフの sequence / curve ポインタは nullptr のまま。
//       ResourceManager から実体を引いて束縛するのは呼び出し側
//       （AnimControllerSystem の BindGraphResources など）。
// ===========================================================================
#include "AnimStateMachine.h"
#include <string>

class AnimGraphSerializer
{
public:
    // グラフを JSON ファイルとして保存する。失敗しても既存のファイルは壊さない
    static bool SaveToJSON(const AnimStateGraph &graph, const std::string &filepath);

    // JSON ファイルからグラフを読み込む
    static bool LoadFromJSON(AnimStateGraph &outGraph, const std::string &filepath);
};