#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <json.hpp>
#include "Engine/Animation/Core/AnimSequence.h"
#include "Engine/Animation/Core/AnimationCurve.h"

// 条件演算子
enum class AnimConditionOp
{
    Equal,
    Greater,
    Less,
    Trigger,
    Finished
};

/// @brief 複数条件の結合方法。AND=全て満たす / OR=いずれか満たす。
enum class AnimTransitionLogic
{
    AND,
    OR
};

// 遷移条件
struct AnimCondition
{
    uint32_t        paramHash = 0;
    std::string     paramName = "NewParam";
    AnimConditionOp op        = AnimConditionOp::Equal;
    float           threshold = 0.0f;
};

// 遷移の矢印
struct AnimTransition
{
    uint32_t targetStateHash = 0;
    float    blendDuration   = 0.2f;

    /// @brief 条件の結合方法（AND/OR）。
    AnimTransitionLogic logicType = AnimTransitionLogic::AND;

    std::vector<AnimCondition> conditions;
};

// ステート (ノード)
struct AnimState
{
    uint32_t    stateHash = 0;
    std::string sequenceName;
    std::string sequenceFilePath;

    bool        isLoop        = true;
    float       playbackSpeed = 1.0f;
    std::string speedCurveName;      // 空文字なら等速
    std::string rootMotionCurveName; // 空文字ならルートモーションカーブなし

    float nodePosX = 0.0f;
    float nodePosY = 0.0f;

    const AnimationCurve *speedCurve = nullptr;
    const AnimSequence   *sequence   = nullptr;

    std::vector<AnimTransition> transitions;
};

// グラフ全体
struct AnimStateGraph
{
    std::vector<AnimState> states;
    uint32_t               entryStateHash = 0;

    float entryPosX = 0.0f;
    float entryPosY = 0.0f;

    /// @brief どのステートからでも遷移可能な特別遷移（Any State）。
    std::vector<AnimTransition> anyStateTransitions;
    float                       anyStatePosX = 0.0f;
    float                       anyStatePosY = 0.0f;
};

// =================================================================
// nlohmann/json 変換
// =================================================================
using json = nlohmann::json;

inline void to_json(json &j, const AnimCondition &c)
{
    j = json{
        {"paramHash", c.paramHash},
        {"paramName", c.paramName},
        {"op", (int)c.op},
        {"threshold", c.threshold}};
}
inline void from_json(const json &j, AnimCondition &c)
{
    j.at("paramHash").get_to(c.paramHash);
    if (j.contains("paramName"))
        j.at("paramName").get_to(c.paramName);
    else
        c.paramName = "";
    int op = 0;
    j.at("op").get_to(op);
    c.op = (AnimConditionOp)op;
    j.at("threshold").get_to(c.threshold);
}

inline void to_json(json &j, const AnimTransition &t)
{
    j = json{
        {"targetStateHash", t.targetStateHash},
        {"blendDuration", t.blendDuration},
        {"logicType", (int)t.logicType},
        {"conditions", t.conditions}};
}
inline void from_json(const json &j, AnimTransition &t)
{
    j.at("targetStateHash").get_to(t.targetStateHash);
    if (j.contains("blendDuration"))
        j.at("blendDuration").get_to(t.blendDuration);
    else
        t.blendDuration = 0.2f;
    if (j.contains("logicType"))
    {
        int l = 0;
        j.at("logicType").get_to(l);
        t.logicType = (AnimTransitionLogic)l;
    }
    else
        t.logicType = AnimTransitionLogic::AND;
    j.at("conditions").get_to(t.conditions);
}

inline void to_json(json &j, const AnimState &s)
{
    j = json{
        {"stateHash", s.stateHash},
        {"sequenceName", s.sequenceName},
        {"sequenceFilePath", s.sequenceFilePath},
        {"isLoop", s.isLoop},
        {"playbackSpeed", s.playbackSpeed},
        {"speedCurveName", s.speedCurveName},
        {"rootMotionCurveName", s.rootMotionCurveName},
        {"nodePosX", s.nodePosX},
        {"nodePosY", s.nodePosY},
        {"transitions", s.transitions}};
}
inline void from_json(const json &j, AnimState &s)
{
    j.at("stateHash").get_to(s.stateHash);
    j.at("sequenceName").get_to(s.sequenceName);
    if (j.contains("sequenceFilePath"))
        j.at("sequenceFilePath").get_to(s.sequenceFilePath);
    if (j.contains("isLoop"))
        j.at("isLoop").get_to(s.isLoop);
    if (j.contains("playbackSpeed"))
        j.at("playbackSpeed").get_to(s.playbackSpeed);
    if (j.contains("speedCurveName"))
        j.at("speedCurveName").get_to(s.speedCurveName);
    if (j.contains("rootMotionCurveName"))
        j.at("rootMotionCurveName").get_to(s.rootMotionCurveName);
    if (j.contains("nodePosX"))
        j.at("nodePosX").get_to(s.nodePosX);
    if (j.contains("nodePosY"))
        j.at("nodePosY").get_to(s.nodePosY);
    j.at("transitions").get_to(s.transitions);
}

inline void to_json(json &j, const AnimStateGraph &g)
{
    j = json{
        {"entryStateHash", g.entryStateHash},
        {"entryPosX", g.entryPosX},
        {"entryPosY", g.entryPosY},
        {"anyStateTransitions", g.anyStateTransitions},
        {"anyStatePosX", g.anyStatePosX},
        {"anyStatePosY", g.anyStatePosY},
        {"states", g.states}};
}
inline void from_json(const json &j, AnimStateGraph &g)
{
    j.at("entryStateHash").get_to(g.entryStateHash);
    if (j.contains("entryPosX"))
        j.at("entryPosX").get_to(g.entryPosX);
    if (j.contains("entryPosY"))
        j.at("entryPosY").get_to(g.entryPosY);
    if (j.contains("anyStateTransitions"))
        j.at("anyStateTransitions").get_to(g.anyStateTransitions);
    if (j.contains("anyStatePosX"))
        j.at("anyStatePosX").get_to(g.anyStatePosX);
    if (j.contains("anyStatePosY"))
        j.at("anyStatePosY").get_to(g.anyStatePosY);
    j.at("states").get_to(g.states);
}

struct AnimStateMachineComponent
{
    std::string    graphPath;
    AnimStateGraph internalGraph;
    uint32_t       currentStateHash = 0;
};