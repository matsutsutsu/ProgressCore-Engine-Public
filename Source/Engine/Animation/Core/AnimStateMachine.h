#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <json.hpp>
#include "Engine/Animation/Core/AnimSequence.h"
#include "Engine/Core/Math/StringHash.h" 

struct CurveData;

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

/// @brief パラメータの型。Unity Animator の Float/Bool/Trigger に相当。
/// @details Int は当面 Float で代用できるため持たない（必要になれば末尾に追加）。
///          シリアライズは int 値。途中挿入禁止・末尾追加のみ。
enum class AnimParamType : uint8_t
{
    Float,
    Bool,
    Trigger
};

/// @brief パラメータの定義（グラフが所有する「宣言」）。
/// @details ランタイムの実値は AnimParametersComponent が持つ。本構造体は名前・型・
///          デフォルト値のみを保持し、エディタのパラメータパネルで編集される。
/// @note    hash は name の派生（HashString(name)）。name 変更時は hash を再計算し、
///          参照している AnimCondition.paramHash も張り替えること（エディタ側責務）。
struct AnimParamDef
{
    std::string   name         = "NewParam";
    uint32_t      hash         = 0;
    AnimParamType type         = AnimParamType::Float;
    float         defaultFloat = 0.0f;  ///< type==Float のときの初期値
    bool          defaultBool  = false; ///< type==Bool のときの初期値
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
    uint32_t                   targetStateHash = 0;
    float                      blendDuration   = 0.2f;
    AnimTransitionLogic        logicType       = AnimTransitionLogic::AND;


    // 文字列を完全に廃止し、ビットマスクに変更
    uint32_t requiredCancelMask = 0;

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
    std::string speedCurveName;
    std::string rootMotionCurveName;

    /// @brief このステートでルートモーション(rootMotionCurve駆動の前進)を適用するか。
    /// @details Unity の "Apply Root Motion" 相当。遷移時に RootMotionComponent.enabled へ反映。
    bool applyRootMotion = false;

    float nodePosX = 0.0f;
    float nodePosY = 0.0f;

    const CurveData  *speedCurve      = nullptr;
    const CurveData  *rootMotionCurve = nullptr; // rootMotionCurveName から束縛
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

    std::vector<AnimTransition> anyStateTransitions;
    float                       anyStatePosX = 0.0f;
    float                       anyStatePosY = 0.0f;

    /// @brief このグラフで使うパラメータ定義の一覧（Unity の Parameters タブ相当）。
    std::vector<AnimParamDef> parameters;
};

// =================================================================
// nlohmann/json 変換
// =================================================================
using json = nlohmann::json;

// --- AnimParamDef ---
inline void to_json(json &j, const AnimParamDef &p)
{
    j = json{
        {"name", p.name},
        {"hash", p.hash},
        {"type", (int)p.type},
        {"defaultFloat", p.defaultFloat},
        {"defaultBool", p.defaultBool}};
}
inline void from_json(const json &j, AnimParamDef &p)
{
    p.name         = j.value("name", std::string{"NewParam"});
    p.hash         = j.value("hash", 0u);
    p.type         = (AnimParamType)j.value("type", 0);
    p.defaultFloat = j.value("defaultFloat", 0.0f);
    p.defaultBool  = j.value("defaultBool", false);
}

// --- AnimCondition ---
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

// --- AnimTransition ---
inline void to_json(json &j, const AnimTransition &t)
{
    j = json{
        {"targetStateHash", t.targetStateHash},
        {"blendDuration", t.blendDuration},
        {"logicType", (int)t.logicType},
        {"requiredCancelMask", t.requiredCancelMask},
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

    // ★ 文字列からの移行対応：もし古いrequiredWindowがあれば無視し、Maskを読む
    t.requiredCancelMask = j.value("requiredCancelMask", 0u);

    j.at("conditions").get_to(t.conditions);
}

// --- AnimState ---
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
        {"applyRootMotion", s.applyRootMotion},
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
    if (j.contains("applyRootMotion"))
        j.at("applyRootMotion").get_to(s.applyRootMotion);
    if (j.contains("nodePosX"))
        j.at("nodePosX").get_to(s.nodePosX);
    if (j.contains("nodePosY"))
        j.at("nodePosY").get_to(s.nodePosY);
    j.at("transitions").get_to(s.transitions);
}

// --- AnimStateGraph ---
inline void to_json(json &j, const AnimStateGraph &g)
{
    j = json{
        {"entryStateHash", g.entryStateHash},
        {"entryPosX", g.entryPosX},
        {"entryPosY", g.entryPosY},
        {"anyStateTransitions", g.anyStateTransitions},
        {"anyStatePosX", g.anyStatePosX},
        {"anyStatePosY", g.anyStatePosY},
        {"parameters", g.parameters},
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
    if (j.contains("parameters"))
        j.at("parameters").get_to(g.parameters);
    j.at("states").get_to(g.states);
}

struct AnimStateMachineComponent
{
    std::string    graphPath;
    AnimStateGraph internalGraph;
    uint32_t       currentStateHash = 0;
};