#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <json.hpp> // cereal を廃止し json.hpp に切り替え
#include "Engine/GamePlay/Animation/AnimSequence.h"

// 条件演算子
// 例: param > threshold なら op は Greater
// 例: param < threshold なら op は Less
// 例: param == threshold なら op は Equal
enum class AnimConditionOp { Equal, Greater, Less };

// 遷移条件
struct AnimCondition {
    uint32_t paramHash;

	// 条件演算子（例: param > threshold なら op は Greater）
    AnimConditionOp op;
    float threshold;
};

// 遷移の矢印
struct AnimTransition {
    uint32_t targetStateHash;

	// その矢印を進むための条件リスト（複数の条件を全て満たす必要がある）
    std::vector<AnimCondition> conditions;
};

// ステート (ノード)
struct AnimState {
    uint32_t stateHash;
    std::string sequenceName; 

    // ランタイム用のポインタ (JSONには保存しない)
    const AnimSequence* sequence = nullptr;

	// そのステートから伸びる遷移のリスト
    std::vector<AnimTransition> transitions;
};

// グラフ全体
struct AnimStateGraph {
    std::vector<AnimState> states;
    uint32_t entryStateHash = 0;
};

// =================================================================
// nlohmann/json 用のシリアライズ定義 (手動変換により制御を容易にする)
// =================================================================
using json = nlohmann::json;

// Condition の変換
inline void to_json(json& j, const AnimCondition& c) {
    j = json{ {"paramHash", c.paramHash}, {"op", (int)c.op}, {"threshold", c.threshold} };
}
inline void from_json(const json& j, AnimCondition& c) {
    j.at("paramHash").get_to(c.paramHash);
    j.at("op").get_to((int&)c.op);
    j.at("threshold").get_to(c.threshold);
}

// Transition の変換
inline void to_json(json& j, const AnimTransition& t) {
    j = json{ {"targetStateHash", t.targetStateHash}, {"conditions", t.conditions} };
}
inline void from_json(const json& j, AnimTransition& t) {
    j.at("targetStateHash").get_to(t.targetStateHash);
    j.at("conditions").get_to(t.conditions);
}

// State の変換
inline void to_json(json& j, const AnimState& s) {
    j = json{ {"stateHash", s.stateHash}, {"sequenceName", s.sequenceName}, {"transitions", s.transitions} };
}
inline void from_json(const json& j, AnimState& s) {
    j.at("stateHash").get_to(s.stateHash);
    j.at("sequenceName").get_to(s.sequenceName);
    j.at("transitions").get_to(s.transitions);
}

// Graph の変換
inline void to_json(json& j, const AnimStateGraph& g) {
    j = json{ {"entryStateHash", g.entryStateHash}, {"states", g.states} };
}
inline void from_json(const json& j, AnimStateGraph& g) {
    j.at("entryStateHash").get_to(g.entryStateHash);
    j.at("states").get_to(g.states);
}

struct AnimStateMachineComponent {
    //　遷移するアニメーションのノード情報のポインタ
	// ポインタの理由は、同じ敵キャラなら全ての個体で同じグラフを共有するため（マスターデータ）
    const AnimStateGraph* graph = nullptr;

	// 現在のステートを示すハッシュ値（ノードID）
    uint32_t currentStateHash = 0;
};