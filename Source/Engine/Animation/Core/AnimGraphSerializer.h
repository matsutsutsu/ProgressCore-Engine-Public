#pragma once
#include "AnimStateMachine.h"
#include <string>

// AnimSequence の解決は名前引きに留め、ResourceManager への依存はここに持ち込まない。
// （保存/読込だけを担い、実体の取得は呼び出し側の責務とする）
class AnimGraphSerializer {
public:
    // グラフをJSONファイルとして保存する
    static bool SaveToJSON(const AnimStateGraph& graph, const std::string& filepath);

    // JSONファイルからグラフを読み込む
    // 引数 availableSequences: ポインタを紐付けるための実体リスト（あるいはResourceManager）
    static bool LoadFromJSON(AnimStateGraph& outGraph, const std::string& filepath, const std::vector<AnimSequence>& availableSequences);
};