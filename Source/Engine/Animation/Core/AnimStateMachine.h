#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <memory> // 階層ステートマシン：AnimState が子グラフを unique_ptr で持つ
#include <json.hpp>
#include "Engine/Animation/Core/AnimSequence.h"
#include "Engine/Core/Math/StringHash.h"

struct CurveData;

// 階層ステートマシンの再帰参照（AnimState が子 AnimStateGraph を持つ）用の前方宣言。
struct AnimStateGraph;

// 条件演算子
enum class AnimConditionOp
{
    Equal,
    Greater,
    Less,
    Trigger,
    Finished
};

// 複数条件の結合方法。AND=全て満たす / OR=いずれか満たす。
enum class AnimTransitionLogic
{
    AND,
    OR
};

// パラメータの型。Unity Animator の Float/Bool/Trigger に相当。
//
// Int は当面 Float で代用できるため持たない（必要になれば末尾に追加）。
// シリアライズは int 値。途中挿入禁止・末尾追加のみ。
enum class AnimParamType : uint8_t
{
    Float,
    Bool,
    Trigger
};

// パラメータの定義（グラフが所有する「宣言」）。
//
// ランタイムの実値は AnimParametersComponent が持つ。本構造体は名前・型・
// デフォルト値のみを保持し、エディタのパラメータパネルで編集される。
//
// hash は name の派生（HashString(name)）。name 変更時は hash を再計算し、
// 参照している AnimCondition.paramHash も張り替えること（エディタ側責務）。
struct AnimParamDef
{
    std::string   name         = "NewParam";
    uint32_t      hash         = 0;
    AnimParamType type         = AnimParamType::Float;
    float         defaultFloat = 0.0f;  // type==Float のときの初期値
    bool          defaultBool  = false; // type==Bool のときの初期値
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
    uint32_t            targetStateHash = 0;
    float               blendDuration   = 0.2f;
    AnimTransitionLogic logicType       = AnimTransitionLogic::AND;

    // この遷移を受け付けてよいキャンセル窓。0 = 常時許可。
    //   非0なら、再生中クリップの ComboWindow/CancelWindow イベントのうち
    //   ビットが1つでも重なる区間の中にいるときだけ遷移できる。
    uint32_t requiredCancelMask = 0;

    // 軽量サブステートマシン用。0=どこからでも（従来の anyState）。
    // 非0の時は「現在ステートの groupId がこの値と一致する場合のみ有効」な
    // グループ・スコープ付き anyState 遷移になる（＝そのグループ内からの割り込み）。
    // 通常ステートの transitions では未使用（0のまま）。anyStateTransitions でのみ意味を持つ。
    uint32_t sourceGroupId = 0;

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

    // このステートでルートモーション(rootMotionCurve駆動の前進)を適用するか。
    //
    // Unity の "Apply Root Motion" 相当。遷移時に RootMotionComponent.enabled へ反映。
    bool applyRootMotion = false;

    // true なら Force Root Lock でルートボーンのYも初期姿勢へピン留めする。
    // 地上攻撃など「浮いてほしくない」ステート用。
    // false（既定）は従来通りYをクリップの動きのまま活かす（ジャンプ攻撃等向け）。
    bool lockVerticalRootMotion = false;

    // 軽量サブステートマシン用のグループID（0=無所属）。同じ groupId のステート群に対し、
    // anyStateTransitions 側の sourceGroupId で「そのグループ限定の割り込み遷移」を1本張れる。
    // 例：Combat 群に groupId=1 を付け、AnyState に「sourceGroupId=1 → Idle [Finished]」を
    // 1本置けば、各攻撃ステートから個別に Idle 線を引かずに済む（減線）。
    uint32_t groupId = 0;

    float nodePosX = 0.0f;
    float nodePosY = 0.0f;

    const CurveData    *speedCurve      = nullptr;
    const CurveData    *rootMotionCurve = nullptr; // rootMotionCurveName から束縛
    const AnimSequence *sequence        = nullptr;

    std::vector<AnimTransition> transitions;

    // =====================================================================
    // 階層ステートマシン（サブステートマシン）
    //   isSubMachine==false（葉）：従来通り sequence を再生するステート。
    //   isSubMachine==true（節）：自分は再生せず、子グラフ(subGraph)を内包する。
    //     ランタイムはこのステートに「入る」と subGraph の entry へ潜り、
    //     エディタはこのノードを「ダブルクリックで潜る」対象として扱う。
    //   ※ AnimStateGraph ⊃ AnimState ⊃ AnimStateGraph の再帰型のため subGraph は
    //     unique_ptr で保持する。値コピー時は subGraph をディープコピーする
    //     （特殊メンバは AnimStateGraph 定義後に out-of-line 定義。pimpl と同型）。
    // =====================================================================
    bool                            isSubMachine = false;
    std::unique_ptr<AnimStateGraph> subGraph; // isSubMachine のときのみ有効

    AnimState();
    ~AnimState();
    AnimState(const AnimState &other);            // ディープコピー（subGraph を複製）
    AnimState &operator=(const AnimState &other); // ディープコピー
    AnimState(AnimState &&) noexcept;             // move は既定
    AnimState &operator=(AnimState &&) noexcept;  // move は既定
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

    // このグラフで使うパラメータ定義の一覧（Unity の Parameters タブ相当）。
    std::vector<AnimParamDef> parameters;
};

// -----------------------------------------------------------------
// AnimState の特殊メンバ（AnimStateGraph が完全型になったここで定義する）。
//   unique_ptr<AnimStateGraph> の複製/破棄には完全型が要るため out-of-line。
//   コピーは subGraph を「新しい実体」として複製するディープコピー。
//   （葉ステートは subGraph==nullptr なので複製コストはゼロ）
// -----------------------------------------------------------------
inline AnimState::AnimState()                                 = default;
inline AnimState::~AnimState()                                = default;
inline AnimState::AnimState(AnimState &&) noexcept            = default;
inline AnimState &AnimState::operator=(AnimState &&) noexcept = default;

inline AnimState::AnimState(const AnimState &o)
    : stateHash(o.stateHash), sequenceName(o.sequenceName), sequenceFilePath(o.sequenceFilePath), isLoop(o.isLoop), playbackSpeed(o.playbackSpeed), speedCurveName(o.speedCurveName), rootMotionCurveName(o.rootMotionCurveName), applyRootMotion(o.applyRootMotion), lockVerticalRootMotion(o.lockVerticalRootMotion), groupId(o.groupId), nodePosX(o.nodePosX), nodePosY(o.nodePosY), speedCurve(o.speedCurve) // 束縛ポインタは共有資源なので浅コピーでよい
      ,
      rootMotionCurve(o.rootMotionCurve), sequence(o.sequence), transitions(o.transitions), isSubMachine(o.isSubMachine), subGraph(o.subGraph ? std::make_unique<AnimStateGraph>(*o.subGraph) : nullptr)
{
}

inline AnimState &AnimState::operator=(const AnimState &o)
{
    if (this == &o) return *this;
    stateHash              = o.stateHash;
    sequenceName           = o.sequenceName;
    sequenceFilePath       = o.sequenceFilePath;
    isLoop                 = o.isLoop;
    playbackSpeed          = o.playbackSpeed;
    speedCurveName         = o.speedCurveName;
    rootMotionCurveName    = o.rootMotionCurveName;
    applyRootMotion        = o.applyRootMotion;
    lockVerticalRootMotion = o.lockVerticalRootMotion;
    groupId                = o.groupId;
    nodePosX               = o.nodePosX;
    nodePosY               = o.nodePosY;
    speedCurve             = o.speedCurve;
    rootMotionCurve        = o.rootMotionCurve;
    sequence               = o.sequence;
    transitions            = o.transitions;
    isSubMachine           = o.isSubMachine;
    subGraph               = o.subGraph ? std::make_unique<AnimStateGraph>(*o.subGraph) : nullptr;
    return *this;
}

// =================================================================
// nlohmann/json 変換
// =================================================================
using json = nlohmann::json;

// 階層（再帰）シリアライズの相互参照解決：AnimState⇄AnimStateGraph の
//   to_json/from_json が互いを呼ぶため、AnimStateGraph 側を前方宣言しておく。
inline void to_json(json &j, const AnimStateGraph &g);
inline void from_json(const json &j, AnimStateGraph &g);

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
        {"sourceGroupId", t.sourceGroupId},
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

    // 文字列からの移行対応：もし古いrequiredWindowがあれば無視し、Maskを読む
    t.requiredCancelMask = j.value("requiredCancelMask", 0u);

    // 軽量サブステートマシン用（未指定の旧データは 0＝従来のどこからでも）。
    t.sourceGroupId = j.value("sourceGroupId", 0u);

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
        {"lockVerticalRootMotion", s.lockVerticalRootMotion},
        {"groupId", s.groupId},
        {"nodePosX", s.nodePosX},
        {"nodePosY", s.nodePosY},
        {"isSubMachine", s.isSubMachine},
        {"transitions", s.transitions}};

    // サブマシンのときだけ子グラフを再帰的に書き出す（葉ステートには出さない）。
    if (s.isSubMachine && s.subGraph)
        j["subGraph"] = *s.subGraph;
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
    if (j.contains("lockVerticalRootMotion"))
        j.at("lockVerticalRootMotion").get_to(s.lockVerticalRootMotion);
    s.groupId = j.value("groupId", 0u); // 軽量サブステートマシン用（旧データは 0＝無所属）
    if (j.contains("nodePosX"))
        j.at("nodePosX").get_to(s.nodePosX);
    if (j.contains("nodePosY"))
        j.at("nodePosY").get_to(s.nodePosY);
    j.at("transitions").get_to(s.transitions);

    // 階層ステートマシン：サブマシンなら子グラフを再帰読込（旧データは false＝葉）。
    s.isSubMachine = j.value("isSubMachine", false);
    if (s.isSubMachine && j.contains("subGraph"))
    {
        s.subGraph = std::make_unique<AnimStateGraph>();
        j.at("subGraph").get_to(*s.subGraph);
    }
    else
    {
        s.subGraph.reset();
    }
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

// 階層のどの深さにある stateHash でも再帰的に探す（葉/サブマシン問わず）。
//   ※ stateHash は名前ハッシュのため、階層をまたいで一意である前提（同名衝突は避ける）。
inline const AnimState *FindStateRecursive(const AnimStateGraph &g, uint32_t hash)
{
    for (const auto &s : g.states)
    {
        if (s.stateHash == hash) return &s;
        if (s.isSubMachine && s.subGraph)
            if (const AnimState *r = FindStateRecursive(*s.subGraph, hash))
                return r;
    }
    return nullptr;
}

struct AnimStateMachineComponent
{
    std::string    graphPath;
    AnimStateGraph internalGraph;

    // 実際に再生中の「葉」ステートの hash（= activePath の末尾）。
    // 他システム(RootMotion/エディタ表示)はこれを「今のステート」として読む。
    uint32_t currentStateHash = 0;

    // 階層ステートマシンの現在地パス [root側..., 葉]。
    // 例: [Combat, Attack_1] = トップで Combat(サブマシン)、その中で Attack_1(葉)。
    // AnimControllerSystem が外→内に評価し毎フレーム維持する。空なら entry から再構築。
    std::vector<uint32_t> activePath;
};