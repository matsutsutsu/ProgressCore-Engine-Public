#pragma once
// ===========================================================================
// 概要: アニメーションのステートマシン（グラフ・ステート・遷移・条件）のデータ定義。
//
// 責務:
//   - AnimStateGraph / AnimState / AnimTransition / AnimCondition / AnimParamDef の定義
//   - nlohmann::json との相互変換（グラフファイルの保存形式）
//   - 実行時の現在地を持つ AnimStateMachineComponent の定義
//
// 備考:
//   - AnimState はサブステートマシンとして子グラフを持てる（階層ステートマシン）。
//   - 評価は AnimControllerSystem、編集は AnimGraphWindow が行う。
// ===========================================================================
#include <vector>
#include <string>
#include <cstdint>
#include <memory>
#include <json.hpp>
#include "Engine/Animation/Core/AnimSequence.h"
#include "Engine/Core/Math/StringHash.h"

struct CurveData;
struct AnimStateGraph; // AnimState が子グラフを持つため先に宣言する

// ===========================================================================
// データ定義
// ===========================================================================

// 遷移条件の演算子
enum class AnimConditionOp
{
    Equal,
    Greater,
    Less,
    Trigger,
    Finished
};

// 複数の条件の結合方法（AND=全て満たす / OR=いずれかを満たす）
enum class AnimTransitionLogic
{
    AND,
    OR
};

// パラメータの型（Unity Animator の Float / Bool / Trigger に相当）。
//   Int は Float で代用できるので持たない。int で保存するので、追加は末尾だけ
enum class AnimParamType : uint8_t
{
    Float,
    Bool,
    Trigger
};

// パラメータの定義（名前・型・初期値）。実行時の値は AnimParametersComponent が持つ。
//   hash は HashString(name)。name を変えたら hash と、それを参照する
//   AnimCondition.paramHash も張り替えること（エディタが行う）
struct AnimParamDef
{
    std::string   name         = "NewParam";
    uint32_t      hash         = 0;
    AnimParamType type         = AnimParamType::Float;
    float         defaultFloat = 0.0f;  // type==Float の初期値
    bool          defaultBool  = false; // type==Bool の初期値
};

// 遷移条件
struct AnimCondition
{
    uint32_t        paramHash = 0;
    std::string     paramName = "NewParam";
    AnimConditionOp op        = AnimConditionOp::Equal;
    float           threshold = 0.0f;
};

// ステート間の遷移
struct AnimTransition
{
    uint32_t            targetStateHash = 0;
    float               blendDuration   = 0.2f;
    AnimTransitionLogic logicType       = AnimTransitionLogic::AND;

    // この遷移を受け付けてよい CancelGroup のマスク。0 なら常に許可。
    //   0 以外なら、再生中クリップの ComboWindow / CancelWindow のうち
    //   ビットが 1 つでも重なる区間の中にいるときだけ遷移できる
    uint32_t requiredCancelMask = 0;

    // anyStateTransitions でだけ使う。0 ならどのステートからでも遷移できる。
    //   0 以外なら、今のステートの groupId が一致するときだけ有効（そのグループ内からの割り込み）
    uint32_t sourceGroupId = 0;

    std::vector<AnimCondition> conditions;
};

// ステート（グラフのノード）
struct AnimState
{
    uint32_t    stateHash = 0;
    std::string sequenceName;
    std::string sequenceFilePath;

    bool        isLoop        = true;
    float       playbackSpeed = 1.0f;
    std::string speedCurveName;
    std::string rootMotionCurveName;

    // このステートでルートモーションを適用するか（Unity の "Apply Root Motion" 相当）。
    //   RootMotionSystem が RootMotionComponent.enabled に反映する
    bool applyRootMotion = false;

    // true なら Force Root Lock でルートボーンの Y もバインドポーズへ固定する（地上攻撃など浮かせたくないステート用）。
    // false ならクリップの Y の動きをそのまま使う（ジャンプ攻撃など）
    bool lockVerticalRootMotion = false;

    // グループ ID（0 は所属なし）。anyStateTransitions の sourceGroupId と組み合わせ、
    //   グループ限定の割り込み遷移を 1 本で張れる。
    //   例: 攻撃ステート群に groupId=1 を付け、AnyState に「sourceGroupId=1 → Idle [Finished]」を
    //       1 本置けば、各攻撃ステートから個別に Idle への遷移を引かずに済む
    uint32_t groupId = 0;

    float nodePosX = 0.0f; // エディタ上のノード位置
    float nodePosY = 0.0f;

    // 名前から束縛した実体（所有しない。ResourceManager が持つ）
    const CurveData    *speedCurve      = nullptr;
    const CurveData    *rootMotionCurve = nullptr;
    const AnimSequence *sequence        = nullptr;

    std::vector<AnimTransition> transitions;

    // サブステートマシン。
    //   isSubMachine==false（葉）: sequence を再生するステート。
    //   isSubMachine==true（節）: 自分は再生せず子グラフ（subGraph）を持つ。
    //     実行時はこのステートに入ると subGraph の entry へ降り、エディタではダブルクリックで中に入る。
    //   AnimStateGraph ⊃ AnimState ⊃ AnimStateGraph の再帰型なので subGraph は unique_ptr で持ち、
    //   コピー時はディープコピーする（特殊メンバは AnimStateGraph の定義後に定義する）
    bool                            isSubMachine = false;
    std::unique_ptr<AnimStateGraph> subGraph; // isSubMachine のときのみ有効

    AnimState();
    ~AnimState();
    AnimState(const AnimState &other);            // ディープコピー（subGraph を複製）
    AnimState &operator=(const AnimState &other); // ディープコピー
    AnimState(AnimState &&) noexcept;
    AnimState &operator=(AnimState &&) noexcept;
};

// グラフ全体（1 階層分）
struct AnimStateGraph
{
    std::vector<AnimState> states;
    uint32_t               entryStateHash = 0;

    float entryPosX = 0.0f; // エディタ上の Entry ノード位置
    float entryPosY = 0.0f;

    std::vector<AnimTransition> anyStateTransitions; // どのステートからでも評価する遷移
    float                       anyStatePosX = 0.0f; // エディタ上の AnyState ノード位置
    float                       anyStatePosY = 0.0f;

    // このグラフで使うパラメータの定義（Unity の Parameters タブ相当）
    std::vector<AnimParamDef> parameters;
};

// AnimState の特殊メンバ。unique_ptr<AnimStateGraph> の複製・破棄には完全型が要るので、
// AnimStateGraph を定義したここで定義する。コピーは subGraph を新しい実体として複製する
// （葉ステートは subGraph==nullptr なので複製しない）
inline AnimState::AnimState()                                 = default;
inline AnimState::~AnimState()                                = default;
inline AnimState::AnimState(AnimState &&) noexcept            = default;
inline AnimState &AnimState::operator=(AnimState &&) noexcept = default;

inline AnimState::AnimState(const AnimState &o)
    : stateHash(o.stateHash), sequenceName(o.sequenceName), sequenceFilePath(o.sequenceFilePath),
      isLoop(o.isLoop), playbackSpeed(o.playbackSpeed), speedCurveName(o.speedCurveName),
      rootMotionCurveName(o.rootMotionCurveName), applyRootMotion(o.applyRootMotion),
      lockVerticalRootMotion(o.lockVerticalRootMotion), groupId(o.groupId),
      nodePosX(o.nodePosX), nodePosY(o.nodePosY),
      // 束縛済みのポインタは共有リソースなので浅いコピーでよい
      speedCurve(o.speedCurve), rootMotionCurve(o.rootMotionCurve), sequence(o.sequence),
      transitions(o.transitions), isSubMachine(o.isSubMachine),
      subGraph(o.subGraph ? std::make_unique<AnimStateGraph>(*o.subGraph) : nullptr)
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

// ===========================================================================
// nlohmann::json 変換
//   読込は、後から追加したキーが無い古いファイルでも読めるよう既定値で補う。
// ===========================================================================
using json = nlohmann::json;

// AnimState と AnimStateGraph の変換関数は互いを呼ぶので、AnimStateGraph 側を先に宣言する
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

    t.requiredCancelMask = j.value("requiredCancelMask", 0u);
    t.sourceGroupId      = j.value("sourceGroupId", 0u);

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

    // サブマシンのときだけ子グラフを再帰的に書き出す
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
    s.groupId = j.value("groupId", 0u);
    if (j.contains("nodePosX"))
        j.at("nodePosX").get_to(s.nodePosX);
    if (j.contains("nodePosY"))
        j.at("nodePosY").get_to(s.nodePosY);
    j.at("transitions").get_to(s.transitions);

    // サブマシンなら子グラフを再帰的に読み込む
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

// ===========================================================================
// 検索・コンポーネント
// ===========================================================================

// どの階層にあるステートでも再帰的に探す（葉・サブマシンを問わない）。
//   stateHash は名前のハッシュなので、階層をまたいでも同名のステートを作らないこと
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

// Entity ごとのステートマシンの状態
struct AnimStateMachineComponent
{
    std::string    graphPath;     // グラフファイルのパス（保存対象）
    AnimStateGraph internalGraph; // 読み込んだグラフ（Entity ごとに持つ）

    // 再生中の葉ステートの hash（activePath の末尾）。
    //   他の System（RootMotion・エディタ表示）はこれを今のステートとして読む
    uint32_t currentStateHash = 0;

    // 階層ステートマシンの現在地 [ルート側..., 葉]。
    //   例: [Combat, Attack_1] はトップで Combat（サブマシン）、その中で Attack_1（葉）。
    //   AnimControllerSystem が毎ステップ維持する。空なら entry から作り直す
    std::vector<uint32_t> activePath;
};