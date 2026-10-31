#include "Editor/Windows/Animation/AnimGraphWindow.h"
#include "Engine/Animation/Core/AnimGraphSerializer.h"
#include "Engine/Core/Math/StringHash.h"
#include <imgui_node_editor.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <string>

#include "Engine/Animation/Components/AnimParametersComponent.h"

#include "Engine/Platform/Core/Dialog.h"
#include "Engine/Platform/Core/Logger.h"
#include <filesystem>
#include "Engine/ECS/Core/CCL_World.h"
#include <cmath>
#include <unordered_map>
#include <sstream>
#include <iomanip>

namespace ed = ax::NodeEditor;

// ---------------------------------------------------------------------------
// ノードエディタの ID の採番
// ---------------------------------------------------------------------------

// ID の下位 2 ビットで種類を分ける（0 = ステートノード / 1 = 遷移の出力ピン / 2 = 遷移の入力ピン / 3 = 予約）。
//   ノードは hash * 4、ピンは linkId * 4 + 1 / + 2、予約 ID（Entry / AnyState）は余り 3 の負値。
//   種類ごとに余りが違うので、種類をまたいで衝突しない

// ノード同士は、ハッシュの上位 2 ビットだけが違うと同じ ID になる（hash * 4 で上位が落ちるため）。
//   32bit のハッシュでは起こる確率が低いので許容している
namespace
{
    inline int StateNodeId(uint32_t h) { return (int)(h * 4u + 0u); }

    // 遷移（リンク）1 本ごとの端点ピン。種は linkId（遷移ごとに一意。理由は DrawNodeWorkspace の 1）
    inline int TransOutPin(int linkId) { return linkId * 4 + 1; }
    inline int TransInPin(int linkId) { return linkId * 4 + 2; }

    // --- 予約 ID（すべて余り 3）---
    constexpr int ENTRY_NODE_ID     = -1;  // 0xFFFFFFFF & 3 == 3
    constexpr int ENTRY_LINK_ID     = -9;  // & 3 == 3
    constexpr int ANY_STATE_NODE_ID = -13; // & 3 == 3

    // リンク ID の開始値。遷移の素性は m_linkLut で引くので、同じフレームの中で一意なら足りる
    constexpr int NORMAL_LINK_BASE = 0x00100000;
    constexpr int ANY_LINK_BASE    = 0x00200000;

    // ツリー全体（root ＋ 全サブグラフ）の遷移リストへ fn を適用する。
    //   パラメータの削除・改名で、階層のどこにある条件も漏れなく張り替えるために使う
    template <class F>
    void ForEachTransitionList(AnimStateGraph &g, F fn)
    {
        for (auto &s : g.states)
        {
            fn(s.transitions);
            if (s.isSubMachine && s.subGraph) ForEachTransitionList(*s.subGraph, fn);
        }
        fn(g.anyStateTransitions);
    }

    // hash のステートを直下に持つ階層を、ツリー全体から探す。無ければ nullptr
    AnimStateGraph *FindOwnerGraph(AnimStateGraph &g, uint32_t hash)
    {
        for (auto &s : g.states)
        {
            if (s.stateHash == hash) return &g;
            if (s.isSubMachine && s.subGraph)
            {
                if (AnimStateGraph *owner = FindOwnerGraph(*s.subGraph, hash)) return owner;
            }
        }
        return nullptr;
    }
} // namespace



// ---------------------------------------------------------------------------
// 実行中の遷移の強調表示（パルス）の状態
// ---------------------------------------------------------------------------
static uint32_t s_prevActiveState = 0;    // 前のフレームで実行中だったステート
static uint32_t s_highlightSrc    = 0;    // 強調する遷移の元
static uint32_t s_highlightDst    = 0;    // 強調する遷移の先
static float    s_highlightTimer  = 0.0f; // 強調の残り時間（秒）



// ---------------------------------------------------------------------------
// 描画の補助
// ---------------------------------------------------------------------------

// p0 → p1 の向きを示す三角形だけを描く（線は描かない）
static void DrawTriangleArrow(ImDrawList *drawList, const ImVec2 &p0, const ImVec2 &p1, ImU32 color)
{
    ImVec2 dir    = ImVec2(p1.x - p0.x, p1.y - p0.y);
    float  length = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    if (length < 0.001f)
        return;
    dir.x /= length;
    dir.y /= length;

    // 矢印は線の真ん中ではなく、遷移先ノードの手前（最大 80px）に置く（Unity と同じ見た目）
    float  offset   = (std::min)(length * 0.8f, 80.0f);
    ImVec2 arrowPos = ImVec2(p1.x - dir.x * offset, p1.y - dir.y * offset);

    float  arrowSize = 7.0f;
    ImVec2 perp      = ImVec2(-dir.y, dir.x);

    ImVec2 pt1 = ImVec2(arrowPos.x + dir.x * arrowSize, arrowPos.y + dir.y * arrowSize);
    ImVec2 pt2 = ImVec2(arrowPos.x - dir.x * arrowSize + perp.x * arrowSize * 0.8f, arrowPos.y - dir.y * arrowSize + perp.y * arrowSize * 0.8f);
    ImVec2 pt3 = ImVec2(arrowPos.x - dir.x * arrowSize - perp.x * arrowSize * 0.8f, arrowPos.y - dir.y * arrowSize - perp.y * arrowSize * 0.8f);

    drawList->AddTriangleFilled(pt1, pt2, pt3, color);
}



// ---------------------------------------------------------------------------
// テキスト出力（グラフの構造を読める形で書き出す）
// ---------------------------------------------------------------------------

// 遷移 1 本（遷移先・ブレンド・条件）を書き出す
static void DumpTransition(std::stringstream &ss, const AnimStateGraph &graph, const AnimTransition &t)
{
    std::string targetName = "Unknown";
    for (const auto &s : graph.states)
    {
        if (s.stateHash == t.targetStateHash) targetName = s.sequenceName;
    }
    ss << "  => " << targetName
       << "  (Blend: " << std::fixed << std::setprecision(2) << t.blendDuration
       << "s, Logic: " << (t.logicType == AnimTransitionLogic::AND ? "AND" : "OR") << ")\n";

    for (const auto &c : t.conditions)
    {
        ss << "     - [" << c.paramName << "] ";
        switch (c.op)
        {
        case AnimConditionOp::Equal:
            ss << "== " << c.threshold;
            break;
        case AnimConditionOp::Greater:
            ss << "> " << c.threshold;
            break;
        case AnimConditionOp::Less:
            ss << "< " << c.threshold;
            break;
        case AnimConditionOp::Trigger:
            ss << "Triggered";
            break;
        case AnimConditionOp::Finished:
            ss << "Finished";
            break;
        }
        ss << "\n";
    }
}



// 1 階層分を書き出し、続けてサブマシンの中身を書き出す。path はこの階層までの経路（"Combat/" など。root は空）
static void DumpAnimGraph(std::stringstream &ss, const AnimStateGraph &graph, const std::string &path)
{
    // 1. Entry State (開始地点) の特定
    std::string entryName = "None";
    for (const auto &s : graph.states)
    {
        if (s.stateHash == graph.entryStateHash)
        {
            entryName = s.sequenceName;
            break;
        }
    }
    ss << "========================================================\n";
    if (!path.empty()) ss << "[ SUB MACHINE ] " << path << "\n";
    ss << "[ ENTRY STATE ] -> " << entryName << "\n";
    ss << "========================================================\n\n";

    // 2. Any State Transitions (特別遷移)
    if (!graph.anyStateTransitions.empty())
    {
        ss << "[ ANY STATE ] (どこからでも遷移可能)\n";
        for (const auto &t : graph.anyStateTransitions)
        {
            DumpTransition(ss, graph, t);
        }
        ss << "\n";
    }

    // 3. 各ステートとそこから伸びる遷移リスト
    for (const auto &state : graph.states)
    {
        if (state.isSubMachine)
        {
            ss << "[ SUB MACHINE ] " << state.sequenceName << "  (中身は後ろに続く)\n";
        }
        else
        {
            ss << "[ STATE ] " << state.sequenceName << "\n";
            ss << "  File:   " << (state.sequenceFilePath.empty() ? "None" : state.sequenceFilePath) << "\n";
            ss << "  Loop:   " << (state.isLoop ? "True" : "False") << "\n";
            ss << "  Speed:  " << std::fixed << std::setprecision(2) << state.playbackSpeed << "x\n";

            if (!state.speedCurveName.empty()) ss << "  Speed Curve: " << state.speedCurveName << "\n";
            if (!state.rootMotionCurveName.empty()) ss << "  RM Curve:    " << state.rootMotionCurveName << "\n";
        }

        if (state.transitions.empty())
        {
            ss << "  -> (No Outgoing Transitions)\n";
        }
        else
        {
            for (const auto &t : state.transitions)
            {
                DumpTransition(ss, graph, t);
            }
        }
        ss << "--------------------------------------------------------\n";
    }

    // 4. サブマシンの中身
    for (const auto &state : graph.states)
    {
        if (!state.isSubMachine || !state.subGraph) continue;

        ss << "\n";
        DumpAnimGraph(ss, *state.subGraph, path + state.sequenceName + "/");
    }
}



// グラフ全体（サブマシンの中も含む）をテキストにする
static std::string DumpAnimGraphToString(const AnimStateGraph &graph)
{
    std::stringstream ss;
    DumpAnimGraph(ss, graph, "");
    return ss.str();
}



// ---------------------------------------------------------------------------
// 生成・破棄
// ---------------------------------------------------------------------------

AnimGraphWindow::AnimGraphWindow() : EditorWindow("Anim Graph Editor")
{
    m_nodeContext = ed::CreateEditor();
}



AnimGraphWindow::~AnimGraphWindow()
{
    if (m_nodeContext) ed::DestroyEditor(m_nodeContext);
}



// ---------------------------------------------------------------------------
// 階層ナビゲーション
// ---------------------------------------------------------------------------

// m_editPath に沿って m_rootGraph を辿り、今編集している階層のグラフを返す
AnimStateGraph &AnimGraphWindow::CurrentView()
{
    // 途中でサブマシンが見つからない（消した等）なら、root へ戻す

    AnimStateGraph *g = &m_rootGraph;
    for (uint32_t h : m_editPath)
    {
        AnimState *sub = nullptr;
        for (auto &s : g->states)
            if (s.stateHash == h && s.isSubMachine && s.subGraph)
            {
                sub = &s;
                break;
            }
        if (!sub)
        {
            m_editPath.clear();
            return m_rootGraph;
        }
        g = sub->subGraph.get();
    }
    return *g;
}



// 指定のサブマシンへ潜る。今の階層に無ければ何もしない
void AnimGraphWindow::EnterSubMachine(uint32_t subMachineHash)
{
    AnimStateGraph &view = CurrentView();
    for (auto &s : view.states)
        if (s.stateHash == subMachineHash && s.isSubMachine && s.subGraph)
        {
            WriteBackNodePositions(); // 今の階層で動かした位置を、離れる前に控える
            ed::ClearSelection();     // 前の階層のノード・リンクの選択を持ち込まない

            m_editPath.push_back(subMachineHash);
            m_selectedNodeId       = 0;
            m_selectedLinkId       = 0;
            m_needRestorePositions = true; // 潜り先のノード位置を復元する
            return;
        }
}



// 今の階層のノード位置を、ノードエディタのライブな値からグラフへ書き戻す
void AnimGraphWindow::WriteBackNodePositions()
{
    if (!m_nodeContext) return;
    ed::SetCurrentEditor(m_nodeContext);

    AnimStateGraph &view = CurrentView();

    // まだ一度も描画していないノードは FLT_MAX が返るので、書き戻さない
    auto ReadLivePos = [](int nodeId, float &outX, float &outY) {
        const ImVec2 pos = ed::GetNodePosition(nodeId);
        if (pos.x == FLT_MAX) return;
        outX = pos.x;
        outY = pos.y;
    };

    ReadLivePos(ENTRY_NODE_ID, view.entryPosX, view.entryPosY);
    ReadLivePos(ANY_STATE_NODE_ID, view.anyStatePosX, view.anyStatePosY);
    for (auto &state : view.states)
    {
        ReadLivePos(StateNodeId(state.stateHash), state.nodePosX, state.nodePosY);
    }
}



// 階層全体で重ならない stateHash を採る。重なれば name に "_2", "_3"... を付け、その名前のハッシュを返す
uint32_t AnimGraphWindow::MakeUniqueStateHash(std::string &name)
{
    // 階層をまたいで同じハッシュがあると、FindStateRecursive が別のステートを返す

    const std::string base = name;
    for (int i = 1; i < 10000; ++i)
    {
        uint32_t h = CCL::Utils::HashString(name.c_str());
        if (!FindStateRecursive(m_rootGraph, h)) return h;
        name = base + "_" + std::to_string(i + 1); // 次の候補名
    }
    return CCL::Utils::HashString(name.c_str());
}



// ステート名を変え、名前のハッシュを stateHash として取り直す
void AnimGraphWindow::RenameState(uint32_t oldHash, std::string newName)
{
    // 処理フロー:
    // 1. 対象のステートと、それを持つ階層を探す
    // 2. 新しい名前から一意なハッシュを採る
    // 3. 参照を張り替える
    // 4. ノードの位置と選択を引き継ぐ

    // ---------------------------------------------------------
    // 1. 対象
    // ---------------------------------------------------------
    AnimStateGraph *owner = FindOwnerGraph(m_rootGraph, oldHash);
    if (!owner || newName.empty()) return;

    AnimState *state = nullptr;
    for (auto &s : owner->states)
    {
        if (s.stateHash == oldHash)
        {
            state = &s;
            break;
        }
    }
    if (state->sequenceName == newName) return;

    // ハッシュを張り替える前に控える。
    //   CurrentView は m_editPath のハッシュで階層を辿るので、張り替えの途中で呼ぶと root へ戻ってしまう
    const bool isInView    = (owner == &CurrentView());
    const bool wasSelected = (m_selectedNodeId == StateNodeId(oldHash));

    // ---------------------------------------------------------
    // 2. ハッシュ
    // ---------------------------------------------------------

    // 名前を元に戻しただけ（ハッシュが今と同じ）なら、名前だけ直す。
    //   一意化に通すと、自分自身と重なったと見なされて連番が付く
    if (CCL::Utils::HashString(newName.c_str()) == oldHash)
    {
        state->sequenceName = newName;
        return;
    }
    const uint32_t newHash = MakeUniqueStateHash(newName);

    // ---------------------------------------------------------
    // 3. 参照の張り替え
    // ---------------------------------------------------------
    auto Patch = [oldHash, newHash](uint32_t &hash) {
        if (hash == oldHash) hash = newHash;
    };

    // 遷移は同じ階層の中にしか張れないので、張り替えるのは owner の中だけでよい
    Patch(owner->entryStateHash);
    for (auto &t : owner->anyStateTransitions)
    {
        Patch(t.targetStateHash);
    }
    for (auto &s : owner->states)
    {
        for (auto &t : s.transitions)
        {
            Patch(t.targetStateHash);
        }
    }

    // 潜っている経路や、作成中の遷移の起点がこのステートなら、それも張り替える
    for (auto &hash : m_editPath)
    {
        Patch(hash);
    }
    Patch(m_transitionSourceHash);

    state->sequenceName = newName;
    state->stateHash    = newHash;

    // ---------------------------------------------------------
    // 4. ノードの位置と選択
    // ---------------------------------------------------------

    // ノード ID はハッシュから作るので、名前を変えると別のノードになる。
    //   今見ている階層なら、古いノードのライブな位置へ新しいノードを置く
    if (isInView)
    {
        const ImVec2 pos = ed::GetNodePosition(StateNodeId(oldHash));
        if (pos.x != FLT_MAX)
        {
            state->nodePosX = pos.x;
            state->nodePosY = pos.y;
        }
        m_nodeToPlaceHash  = newHash;
        m_selectPlacedNode = wasSelected;
    }
}



// 入力途中のステート名を確定する
void AnimGraphWindow::CommitStateName()
{
    if (m_stateNameEdited) RenameState(m_stateNameHash, m_stateNameBuf);

    // 次に描くとき、確定後の名前（連番が付くこともある）で入力欄を作り直させる
    m_stateNameEdited = false;
    m_stateNameHash   = 0;
}



// 「Root > AttackCombo > ...」のパンくずを描く。クリックで浅い階層へ戻る
void AnimGraphWindow::DrawBreadcrumb()
{
    if (ImGui::Button("Root"))
    {
        WriteBackNodePositions(); // 今の階層で動かした位置を、離れる前に控える
        ed::ClearSelection();
        m_editPath.clear();
        m_needRestorePositions = true;
    }

    AnimStateGraph *g = &m_rootGraph;
    for (size_t i = 0; i < m_editPath.size(); ++i)
    {
        const char *name = "?";
        AnimState  *sub  = nullptr;
        for (auto &s : g->states)
            if (s.stateHash == m_editPath[i])
            {
                name = s.sequenceName.c_str();
                sub  = &s;
                break;
            }

        ImGui::SameLine();
        ImGui::TextDisabled(">");
        ImGui::SameLine();
        ImGui::PushID((int)i);
        const bool clicked = ImGui::Button(name);
        ImGui::PopID();
        if (clicked)
        {
            WriteBackNodePositions();
            ed::ClearSelection();
            m_editPath.resize(i + 1); // この段まで戻る
            m_needRestorePositions = true;
            break;
        }
        if (sub && sub->subGraph)
            g = sub->subGraph.get();
        else
            break;
    }

    // 潜っている階層に Entry が無いと、そのサブマシンに入っても何も再生されないので警告する
    AnimStateGraph &view = CurrentView();
    if (!m_editPath.empty() && view.entryStateHash == 0 && !view.states.empty())
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f),
                           "  [!] Entry 未設定（ノード右クリック → Set as Entry）");
    }
}



// ---------------------------------------------------------------------------
// ノードの作業領域（描画と操作）
// ---------------------------------------------------------------------------

void AnimGraphWindow::DrawContents(EditorContext &context)
{
    // 処理フロー:
    // 1. 前のフレームで予約したサブマシンへ潜る
    // 2. 選択中のエンティティで実行中のステートを読む
    // 3. ツールバーとパンくずを描く
    // 4. 左にノードの作業領域、右にパラメータとインスペクタを描く

    ed::SetCurrentEditor(m_nodeContext);

    // ---------------------------------------------------------
    // 1. 潜る予約
    // ---------------------------------------------------------

    // 描画中に m_editPath を書き換えると参照が飛ぶので、フレームの頭で処理する
    if (m_pendingEnterSub != 0)
    {
        EnterSubMachine(m_pendingEnterSub);
        m_pendingEnterSub = 0;
    }

    // ---------------------------------------------------------
    // 2. 実行中のステート
    // ---------------------------------------------------------

    // ノードの強調表示に使う。読むのは葉のステートだけ
    m_runtimeActiveStateHash = 0;
    if (context.selectedEntity != CCL::ECS::InvalidEntityID && context.world)
    {
        auto *fsm = context.world->GetComponent<AnimStateMachineComponent>(context.selectedEntity);
        if (fsm)
        {
            m_runtimeActiveStateHash = fsm->currentStateHash;
        }
    }

    // ---------------------------------------------------------
    // 3. ツールバーとパンくず
    // ---------------------------------------------------------
    DrawToolbar();
    DrawBreadcrumb();

    // ---------------------------------------------------------
    // 4. 作業領域・パラメータ・インスペクタ
    // ---------------------------------------------------------

    // カラム幅の上書きは初回だけにし、境界線をドラッグできるようにする
    static bool initColumns = true;
    ImGui::Columns(2, "AnimGraphColumns");
    if (initColumns)
    {
        ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.75f);
        initColumns = false;
    }

    // ノードエディタの座標系（キャンバス座標）が外へ漏れないよう、ChildWindow で囲む
    ImGui::BeginChild("NodeWorkspaceChild", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    DrawNodeWorkspace();
    ImGui::EndChild();

    ImGui::NextColumn();

    // 右カラムは上がパラメータ、下がインスペクタ（Unity Animator と同じ並び）。
    //   パラメータの一覧が潰れて見えなくならないよう、上に 55% を割り当てる
    const float paramPanelHeight = ImGui::GetContentRegionAvail().y * 0.55f;

    ImGui::BeginChild("ParameterPanelChild", ImVec2(0, paramPanelHeight), true);
    DrawParameterPanel(context);
    ImGui::EndChild();

    ImGui::BeginChild("InspectorChild", ImVec2(0, 0), false);
    DrawInspector();
    ImGui::EndChild();

    ImGui::Columns(1);
}



// 今の階層のノードとリンクを描き、遷移の作成・削除・右クリックを処理する
void AnimGraphWindow::DrawNodeWorkspace()
{
    // 処理フロー:
    // 1. ピンの採番規則を決める
    // 2. 全遷移を並べ、ノードの中心を求める関数を用意する
    // 3. ノードを描く（ピンは相手のノードの方向に置く）
    // 4. 実行中のステートの変化を見て、遷移の強調を始める
    // 5. リンクを描き、リンク ID → 遷移の対応表を作る
    // 6. 遷移の作成中なら、仮の線を描いて遷移先のクリックを受け付ける
    // 7. 削除・右クリックを処理し、選択を読む

    // 今見ている階層（以降の m_currentGraph はメンバではなく、この局所参照）
    AnimStateGraph &m_currentGraph = CurrentView();

    ed::Begin("My Anim Node Editor");

    ed::PushStyleVar(ed::StyleVar_LinkStrength, 0.0f); // 直線
    ed::PushStyleVar(ed::StyleVar_NodeRounding, 12.0f);
    ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(0, 0, 0, 0));

    constexpr float NODE_WIDTH           = 150.0f;
    constexpr float NODE_HEIGHT_ESTIMATE = 42.0f; // ノードの概算高さ

    std::unordered_map<int, ImVec2> pinPositions;
    ImDrawList                     *drawList = ImGui::GetWindowDrawList();

    // ---------------------------------------------------------
    // 1. ピンの採番
    // ---------------------------------------------------------

    // ピン ID は遷移ごとに一意な linkId から作る。
    //   (src, dst) の組から作ると、同じ元 → 先に遷移を 2 本張ったとき同じピン ID になる

    // node-editor は同じフレームで同じ ID の BeginPin を 2 回受け取ると内部の対応表が壊れ、
    //   グラフを開いた瞬間にフリーズする
    auto GetOutPinId = [](int linkId) -> int { return TransOutPin(linkId); };
    auto GetInPinId  = [](int linkId) -> int { return TransInPin(linkId); };

    // ---------------------------------------------------------
    // 2. 全遷移とノードの中心
    // ---------------------------------------------------------

    // 全遷移を先に並べておく（ノードを描くときに、相手のノードの方向へピンを置くため）
    struct TransInfo
    {
        uint32_t src;
        uint32_t dst;
        bool     isAny;
        int      linkId;
    };
    std::vector<TransInfo> allTrans;

    int      linkIdCounter = NORMAL_LINK_BASE;
    uint32_t ENTRY_HASH    = 0;
    uint32_t ANY_HASH      = 0xFFFFFFFF;

    if (m_currentGraph.entryStateHash != 0)
        allTrans.push_back({ENTRY_HASH, m_currentGraph.entryStateHash, false, ENTRY_LINK_ID});

    for (const auto &trans : m_currentGraph.anyStateTransitions)
        allTrans.push_back({ANY_HASH, trans.targetStateHash, true, ANY_LINK_BASE + (linkIdCounter++)});

    for (const auto &s : m_currentGraph.states)
    {
        for (const auto &trans : s.transitions)
        {
            allTrans.push_back({s.stateHash, trans.targetStateHash, false, linkIdCounter++});
        }
    }

    // ノードの中心を、ノードエディタのライブな位置とサイズから求める（キャンバス座標）。
    //   保存済みの nodePosX/Y を使うと、ドラッグした直後に古い位置から線が伸びる
    auto GetNodeCenter = [&](uint32_t hash) -> ImVec2 {
        int nodeId;
        if (hash == ENTRY_HASH)
            nodeId = ENTRY_NODE_ID;
        else if (hash == ANY_HASH)
            nodeId = ANY_STATE_NODE_ID;
        else
            nodeId = StateNodeId(hash);

        ImVec2 pos  = ed::GetNodePosition(nodeId); // キャンバス座標（ライブ）
        ImVec2 size = ed::GetNodeSize(nodeId);     // 実サイズ（ライブ）

        // サイズが未確定（描画前）なら推定値でフォールバック
        if (size.x <= 0.0f)
            size.x = NODE_WIDTH;
        if (size.y <= 0.0f)
            size.y = NODE_HEIGHT_ESTIMATE;

        return ImVec2(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
    };

    // 進行方向に対して左へ 8px ずらすオフセット（往復の遷移の線が重ならないように）
    auto CalcNormalOffset = [](ImVec2 p0, ImVec2 p1) -> ImVec2 {
        ImVec2 dir = ImVec2(p1.x - p0.x, p1.y - p0.y);
        float  len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len < 0.001f)
            return ImVec2(0, 0);
        dir.x /= len;
        dir.y /= len;
        return ImVec2(-dir.y * 8.0f, dir.x * 8.0f);
    };

    // ---------------------------------------------------------
    // 3. ノード
    // ---------------------------------------------------------

    // ノードを 1 つ描き、そのノードが端になる遷移のピンを相手の方向に置く
    auto DrawBlockNode = [&](int nodeId, uint32_t nodeHash, const char *title, ImVec4 bgColor, bool isActive) {
        // 強調の優先度: 実行中（青のパルス） > エディタで選択中（黄） > 通常
        const bool isSelected = (m_selectedNodeId == nodeId);

        ed::PushStyleColor(ed::StyleColor_NodeBg, bgColor);

        int pushedColors = 1; // NodeBg は常に push
        int pushedVars   = 0;

        if (isActive)
        {
            float pulse = (sinf((float)ImGui::GetTime() * 8.0f) + 1.0f) * 0.5f;
            ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(0.4f + pulse * 0.4f, 0.8f + pulse * 0.2f, 1.0f, 1.0f));
            ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 2.0f + pulse * 1.5f);
            ++pushedColors;
            ++pushedVars;
        }
        else if (isSelected)
        {
            // エディタ選択中: 黄色の太枠（実行中でないときのみ）
            ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(1.0f, 0.85f, 0.2f, 1.0f));
            ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 3.0f);
            ++pushedColors;
            ++pushedVars;
        }

        ed::BeginNode(nodeId);

        ImVec2 startPos  = ImGui::GetCursorPos();
        ImVec2 screenPos = ImGui::GetCursorScreenPos();

        ImGui::BeginGroup();
        ImGui::Dummy(ImVec2(NODE_WIDTH, 14));
        std::string displayText = isActive ? (std::string("▶ ") + title) : title;
        float       textWidth   = ImGui::CalcTextSize(displayText.c_str()).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.0f, (NODE_WIDTH - textWidth) * 0.5f));
        ImGui::TextColored(isActive ? ImVec4(0.8f, 1.0f, 0.8f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", displayText.c_str());
        ImGui::Dummy(ImVec2(NODE_WIDTH, 14));
        ImGui::EndGroup();

        float  nodeHeight = ImGui::GetCursorPos().y - startPos.y;
        ImVec2 myCenter   = GetNodeCenter(nodeHash);

        for (const auto &t : allTrans)
        {
            if (t.src == nodeHash)
            {
                ImVec2 targetCenter = GetNodeCenter(t.dst);
                ImVec2 offset       = CalcNormalOffset(myCenter, targetCenter);

                ImVec2 pinLocal = ImVec2(startPos.x + NODE_WIDTH / 2 + offset.x, startPos.y + nodeHeight / 2 + offset.y);
                ImGui::SetCursorPos(pinLocal);
                ed::BeginPin(GetOutPinId(t.linkId), ed::PinKind::Output);
                ImGui::Dummy(ImVec2(1, 1));
                ed::EndPin();
                pinPositions[GetOutPinId(t.linkId)] = ImVec2(screenPos.x + NODE_WIDTH / 2 + offset.x, screenPos.y + nodeHeight / 2 + offset.y);
            }
            if (t.dst == nodeHash)
            {
                ImVec2 srcCenter = GetNodeCenter(t.src);
                ImVec2 offset    = CalcNormalOffset(srcCenter, myCenter);

                ImVec2 pinLocal = ImVec2(startPos.x + NODE_WIDTH / 2 + offset.x, startPos.y + nodeHeight / 2 + offset.y);
                ImGui::SetCursorPos(pinLocal);
                ed::BeginPin(GetInPinId(t.linkId), ed::PinKind::Input);
                ImGui::Dummy(ImVec2(1, 1));
                ed::EndPin();
                pinPositions[GetInPinId(t.linkId)] = ImVec2(screenPos.x + NODE_WIDTH / 2 + offset.x, screenPos.y + nodeHeight / 2 + offset.y);
            }
        }

        ed::EndNode();

        // push した数だけ正確に pop（スタックバランスを厳守）
        for (int i = 0; i < pushedVars; ++i)
            ed::PopStyleVar();
        for (int i = 0; i < pushedColors; ++i)
            ed::PopStyleColor();
    };

    DrawBlockNode(ENTRY_NODE_ID, ENTRY_HASH, "Entry", ImVec4(0.15f, 0.6f, 0.15f, 1.0f), false);
    if (m_needRestorePositions)
        ed::SetNodePosition(ENTRY_NODE_ID, ImVec2(m_currentGraph.entryPosX, m_currentGraph.entryPosY));

    DrawBlockNode(ANY_STATE_NODE_ID, ANY_HASH, "Any State", ImVec4(0.1f, 0.6f, 0.6f, 1.0f), false);
    if (m_needRestorePositions)
        ed::SetNodePosition(ANY_STATE_NODE_ID, ImVec2(m_currentGraph.anyStatePosX, m_currentGraph.anyStatePosY));

    for (auto &state : m_currentGraph.states)
    {
        const bool isEntry  = (m_currentGraph.entryStateHash == state.stateHash);
        const bool isActive = (m_runtimeActiveStateHash == state.stateHash);

        // サブマシンは紫系で色分けし、名前に [Sub] を付けて潜れることを示す
        ImVec4 nodeBgColor;
        if (state.isSubMachine)
            nodeBgColor = ImVec4(0.32f, 0.24f, 0.48f, 1.0f);
        else
            nodeBgColor = isEntry ? ImVec4(0.9f, 0.5f, 0.1f, 1.0f)
                                  : (isActive ? ImVec4(0.2f, 0.5f, 0.8f, 1.0f)
                                              : ImVec4(0.35f, 0.35f, 0.35f, 1.0f));

        std::string title = state.isSubMachine ? ("[Sub] " + state.sequenceName) : state.sequenceName;
        DrawBlockNode(StateNodeId(state.stateHash), state.stateHash, title.c_str(), nodeBgColor, isActive);
        if (m_needRestorePositions)
            ed::SetNodePosition(StateNodeId(state.stateHash), ImVec2(state.nodePosX, state.nodePosY));
    }

    if (m_nodeToPlaceHash != 0)
    {
        for (const auto &s : m_currentGraph.states)
        {
            if (s.stateHash == m_nodeToPlaceHash)
            {
                ed::SetNodePosition(StateNodeId(m_nodeToPlaceHash), ImVec2(s.nodePosX, s.nodePosY));
                break;
            }
        }

        // 改名で置き直しただけで、選ばれていなかったノードは選択しない
        if (m_selectPlacedNode) ed::SelectNode(StateNodeId(m_nodeToPlaceHash));
        m_nodeToPlaceHash  = 0;
        m_selectPlacedNode = true;
    }

    // ---------------------------------------------------------
    // 4. 遷移の強調
    // ---------------------------------------------------------

    // 実行中のステートが変わったら、その遷移を短い間だけ強調する
    if (m_runtimeActiveStateHash != s_prevActiveState)
    {
        if (s_prevActiveState != 0 && m_runtimeActiveStateHash != 0)
        {
            s_highlightSrc = s_prevActiveState;
            s_highlightDst = m_runtimeActiveStateHash;

            // 遷移が続けて起きても強調が重ならないよう、0.3 秒と短くする
            s_highlightTimer = 0.3f;
        }
        s_prevActiveState = m_runtimeActiveStateHash;
    }
    if (s_highlightTimer > 0.0f)
    {
        s_highlightTimer -= ImGui::GetIO().DeltaTime;
        if (s_highlightTimer < 0.0f)
            s_highlightTimer = 0.0f;
    }

    // ---------------------------------------------------------
    // 5. リンク
    // ---------------------------------------------------------

    // 当たり判定と描画順は node-editor に任せる
    m_linkLut.clear();
    for (const auto &t : allTrans)
    {
        int srcPin = GetOutPinId(t.linkId);
        int dstPin = GetInPinId(t.linkId);

        bool isSelected = (m_selectedLinkId == t.linkId);

        // --- 強調する遷移か ---
        bool isJustTransitioned = false;
        if (s_highlightTimer > 0.0f)
        {
            if (t.src == s_highlightSrc && t.dst == s_highlightDst)
            {
                isJustTransitioned = true;
            }
            else if (t.isAny && t.dst == s_highlightDst)
            {
                // 通常の遷移が無いときだけ、AnyState からの遷移として強調する
                bool hasNormal = false;
                for (const auto &tr : allTrans)
                {
                    if (!tr.isAny && tr.src == s_highlightSrc && tr.dst == s_highlightDst)
                    {
                        hasNormal = true;
                        break;
                    }
                }
                if (!hasNormal)
                    isJustTransitioned = true;
            }
        }

        // --- 色と太さ ---
        ImVec4 linkColor = t.isAny ? ImVec4(0.4f, 0.8f, 0.8f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f);

        if (m_runtimeActiveStateHash == t.src)
        {
            linkColor = ImVec4(0.3f, 0.7f, 1.0f, 1.0f); // 実行中のステートから伸びる線は青
        }

        // 選択中か遷移した直後は、オレンジで太くする
        if (isSelected || isJustTransitioned)
        {
            linkColor = ImVec4(1.0f, 0.65f, 0.0f, 1.0f);
        }

        float thickness = (isSelected || isJustTransitioned) ? 4.0f : 2.5f;

        ed::Link(t.linkId, srcPin, dstPin, linkColor, thickness);

        m_linkLut.push_back({t.linkId, LinkRef{t.src, t.dst, t.isAny}});

        // 矢印は node-editor が描かないので自前で描く
        if (pinPositions.count(srcPin) && pinPositions.count(dstPin))
        {
            DrawTriangleArrow(drawList, pinPositions[srcPin], pinPositions[dstPin], ImGui::ColorConvertFloat4ToU32(linkColor));
        }
    }

    // 対応表ができたので、遷移の増減でずれたリンクの選択を付け直す
    SyncSelectedLink();

    // ---------------------------------------------------------
    // 6. 遷移の作成中（Make Transition）
    // ---------------------------------------------------------
    if (m_editorMode == EditorMode::CreatingTransition)
    {
        uint32_t srcHash = m_isTransitionSourceAnyState ? ANY_HASH : m_transitionSourceHash;

        int srcNodeId = m_isTransitionSourceAnyState ? ANY_STATE_NODE_ID
                                                     : (srcHash == ENTRY_HASH ? ENTRY_NODE_ID : StateNodeId(srcHash));

        ed::Suspend();
        ImDrawList *overlay = ImGui::GetForegroundDrawList();

        // ソースノードの矩形を「スクリーン座標」で求める。
        //   GetNodeSize はキャンバス座標サイズなので、左上(min)と右下(max)を
        //   それぞれ CanvasToScreen して引き算し、ズーム適用後の実サイズを得る。
        ImVec2 srcMinCanvas  = ed::GetNodePosition(srcNodeId);
        ImVec2 srcSizeCanvas = ed::GetNodeSize(srcNodeId);
        if (srcSizeCanvas.x <= 0.0f)
            srcSizeCanvas.x = NODE_WIDTH;
        if (srcSizeCanvas.y <= 0.0f)
            srcSizeCanvas.y = NODE_HEIGHT_ESTIMATE;

        ImVec2 srcMin = ed::CanvasToScreen(srcMinCanvas);
        ImVec2 srcMax = ed::CanvasToScreen(ImVec2(srcMinCanvas.x + srcSizeCanvas.x,
                                                  srcMinCanvas.y + srcSizeCanvas.y));
        ImVec2 srcCenter((srcMin.x + srcMax.x) * 0.5f, (srcMin.y + srcMax.y) * 0.5f);
        ImVec2 mousePos = ImGui::GetMousePos();

        // 中心からマウス方向へ伸ばし、ノード矩形の外周との交点を始点にする。
        //   矩形と線分の交差を、方向ベクトルを矩形の半サイズでスケールして求める。
        auto EdgePoint = [](ImVec2 c, ImVec2 halfSize, ImVec2 dir) -> ImVec2 {
            // dir 方向に矩形外周まで伸ばすスケール t を求める（軸ごとに最小を取る）
            float tx = (std::abs(dir.x) > 1e-4f) ? (halfSize.x / std::abs(dir.x)) : FLT_MAX;
            float ty = (std::abs(dir.y) > 1e-4f) ? (halfSize.y / std::abs(dir.y)) : FLT_MAX;
            float t  = (std::min)(tx, ty);
            return ImVec2(c.x + dir.x * t, c.y + dir.y * t);
        };

        ImVec2 dir(mousePos.x - srcCenter.x, mousePos.y - srcCenter.y);
        float  len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len > 1e-4f)
        {
            dir.x /= len;
            dir.y /= len;
        }
        else
        {
            dir = ImVec2(1, 0);
        }

        ImVec2 srcHalf((srcMax.x - srcMin.x) * 0.5f, (srcMax.y - srcMin.y) * 0.5f);
        ImVec2 startPos = EdgePoint(srcCenter, srcHalf, dir); // 外周から出す

        // 接続先候補のハイライト：ノードにぴったり合わせた枠（スクリーン実サイズ）
        ImU32      lineColor = IM_COL32(255, 255, 255, 255);
        ImVec2     endPos    = mousePos;
        ed::NodeId hovered   = ed::GetHoveredNode();
        if (hovered.Get() != 0)
        {
            int  hid         = (int)(intptr_t)hovered.Get();
            bool validTarget = (hid != ENTRY_NODE_ID && hid != ANY_STATE_NODE_ID && hid != srcNodeId);

            // ホバーノードの矩形もスクリーン座標で正確に求める
            ImVec2 hMinCanvas  = ed::GetNodePosition(hid);
            ImVec2 hSizeCanvas = ed::GetNodeSize(hid);
            ImVec2 hMin        = ed::CanvasToScreen(hMinCanvas);
            ImVec2 hMax        = ed::CanvasToScreen(ImVec2(hMinCanvas.x + hSizeCanvas.x,
                                                           hMinCanvas.y + hSizeCanvas.y));

            ImU32 hl = validTarget ? IM_COL32(80, 220, 120, 255) : IM_COL32(220, 80, 80, 255);
            // ノード枠ぴったり（はみ出さないよう内側1px）
            overlay->AddRect(ImVec2(hMin.x + 1, hMin.y + 1), ImVec2(hMax.x - 1, hMax.y - 1),
                             hl, 6.0f, 0, 2.5f);

            if (validTarget)
                lineColor = IM_COL32(120, 240, 160, 255);

            // 線の終点もホバーノードの外周に吸着させる（マウスではなくノード縁へ）
            ImVec2 hCenter((hMin.x + hMax.x) * 0.5f, (hMin.y + hMax.y) * 0.5f);
            ImVec2 hHalf((hMax.x - hMin.x) * 0.5f, (hMax.y - hMin.y) * 0.5f);
            ImVec2 dirToSrc(srcCenter.x - hCenter.x, srcCenter.y - hCenter.y);
            float  l2 = std::sqrt(dirToSrc.x * dirToSrc.x + dirToSrc.y * dirToSrc.y);
            if (l2 > 1e-4f)
            {
                dirToSrc.x /= l2;
                dirToSrc.y /= l2;
            }
            endPos = EdgePoint(hCenter, hHalf, dirToSrc);
        }

        overlay->AddLine(startPos, endPos, lineColor, 2.5f);
        DrawTriangleArrow(overlay, startPos, endPos, lineColor);

        ed::Resume();

        // 左クリックで遷移先を確定する
        if (ImGui::IsMouseClicked(0))
        {
            ed::NodeId hoveredNode = ed::GetHoveredNode();
            if (hoveredNode.Get() != 0)
            {
                int      rawId      = (int)(intptr_t)hoveredNode.Get();
                uint32_t targetHash = 0;
                if (rawId != ENTRY_NODE_ID && rawId != ANY_STATE_NODE_ID)
                {
                    for (const auto &s : m_currentGraph.states)
                    {
                        if (StateNodeId(s.stateHash) == rawId)
                        {
                            targetHash = s.stateHash;
                            break;
                        }
                    }
                }

                if (targetHash != 0 && targetHash != srcHash)
                {
                    if (m_isTransitionSourceAnyState)
                    {
                        bool exists = false;
                        for (auto &t : m_currentGraph.anyStateTransitions)
                            if (t.targetStateHash == targetHash)
                                exists = true;
                        if (!exists)
                        {
                            AnimTransition t;
                            t.targetStateHash = targetHash;
                            m_currentGraph.anyStateTransitions.push_back(t);
                        }
                    }
                    else
                    {
                        for (auto &s : m_currentGraph.states)
                        {
                            if (s.stateHash == srcHash)
                            {
                                bool exists = false;
                                for (auto &t : s.transitions)
                                    if (t.targetStateHash == targetHash)
                                        exists = true;
                                if (!exists)
                                {
                                    AnimTransition t;
                                    t.targetStateHash = targetHash;
                                    s.transitions.push_back(t);
                                }
                                break;
                            }
                        }
                    }
                }
            }
            m_editorMode = EditorMode::Normal;
        }
        else if (ImGui::IsMouseClicked(1) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            m_editorMode = EditorMode::Normal;
        }
    }

    ed::PopStyleVar(3);

    if (m_needRestorePositions)
    {
        ed::NavigateToContent();
        m_needRestorePositions = false;
    }

    // ---------------------------------------------------------
    // 7. 操作と選択
    // ---------------------------------------------------------
    HandleInteraction();
    HandleContextMenu();

    ed::End();

    // 選択を読む（インスペクタの表示と、次のフレームのリンクの付け直しに使う）
    ed::NodeId selNode;
    ed::LinkId selLink;
    m_selectedNodeId = 0;
    m_selectedLinkId = 0;
    if (ed::GetSelectedNodes(&selNode, 1) > 0)
        m_selectedNodeId = (int)(intptr_t)selNode.Get();
    else if (ed::GetSelectedLinks(&selLink, 1) > 0)
        m_selectedLinkId = (int)(intptr_t)selLink.Get();

    // 次のフレームで ID がずれていないか比べるため、今指している遷移を控える
    if (const LinkRef *ref = FindLink(m_selectedLinkId)) m_selectedLinkRef = *ref;
}



// 選択中のリンクを、前のフレームと同じ遷移へ付け直す
void AnimGraphWindow::SyncSelectedLink()
{
    // リンク ID は描画順の連番なので、遷移を足したり消したりすると同じ ID が別の遷移を指す。
    //   付け直さないと、インスペクタが黙って別の遷移を表示・編集してしまう
    if (m_selectedLinkId == 0) return;

    const LinkRef *now = FindLink(m_selectedLinkId);
    if (now && *now == m_selectedLinkRef) return;

    for (const auto &[id, ref] : m_linkLut)
    {
        if (ref == m_selectedLinkRef)
        {
            ed::SelectLink(id);
            m_selectedLinkId = id;
            return;
        }
    }

    // 同じ遷移がもう無い（消した・Entry を付け替えた）なら選択を外す
    ed::ClearSelection();
    m_selectedLinkId = 0;
}



// ダブルクリックでサブマシンへ潜る予約と、リンク・ノードの削除を処理する
void AnimGraphWindow::HandleInteraction()
{
    // 処理フロー:
    // 1. ダブルクリックしたサブマシンへ潜る予約をする
    // 2. 削除されたリンクの遷移を消す
    // 3. 削除されたノードと、それへ入る遷移を消す

    AnimStateGraph &m_currentGraph = CurrentView(); // 現在の階層を編集対象にする

    // ---------------------------------------------------------
    // 1. ダブルクリック
    // ---------------------------------------------------------

    // 潜るのは次のフレームの頭（描画の途中で m_editPath を変えないため）
    if (ed::NodeId dbl = ed::GetDoubleClickedNode())
    {
        int raw = (int)(intptr_t)dbl.Get();
        for (auto &s : m_currentGraph.states)
            if (StateNodeId(s.stateHash) == raw && s.isSubMachine)
            {
                m_pendingEnterSub = s.stateHash;
                break;
            }
    }

    // ---------------------------------------------------------
    // 2. リンクの削除
    // ---------------------------------------------------------
    if (ed::BeginDelete())
    {
        ed::LinkId linkId;
        while (ed::QueryDeletedLink(&linkId))
        {
            if (ed::AcceptDeletedItem())
            {
                int id = (int)(intptr_t)linkId.Get();
                if (id == ENTRY_LINK_ID)
                {
                    m_currentGraph.entryStateHash = 0;
                }
                else if (const LinkRef *ref = FindLink(id))
                {
                    if (ref->isAnyState)
                    {
                        uint32_t dst = ref->dstHash;
                        m_currentGraph.anyStateTransitions.erase(
                            std::remove_if(m_currentGraph.anyStateTransitions.begin(), m_currentGraph.anyStateTransitions.end(),
                                           [dst](const AnimTransition &t) { return t.targetStateHash == dst; }),
                            m_currentGraph.anyStateTransitions.end());
                    }
                    else
                    {
                        uint32_t src = ref->srcHash, dst = ref->dstHash;
                        for (auto &s : m_currentGraph.states)
                        {
                            if (s.stateHash == src)
                            {
                                s.transitions.erase(
                                    std::remove_if(s.transitions.begin(), s.transitions.end(),
                                                   [dst](const AnimTransition &t) { return t.targetStateHash == dst; }),
                                    s.transitions.end());
                                break;
                            }
                        }
                    }
                }
            }
        }

        // ---------------------------------------------------------
        // 3. ノードの削除
        // ---------------------------------------------------------

        // Entry と AnyState は消させない
        ed::NodeId nodeId;
        while (ed::QueryDeletedNode(&nodeId))
        {
            int rawId = (int)(intptr_t)nodeId.Get();
            if (rawId == ENTRY_NODE_ID || rawId == ANY_STATE_NODE_ID)
            {
                ed::RejectDeletedItem();
                continue;
            }
            if (ed::AcceptDeletedItem())
            {
                uint32_t targetHash = 0;
                bool     found      = false;
                for (auto &s : m_currentGraph.states)
                {
                    if (StateNodeId(s.stateHash) == rawId)
                    {
                        targetHash = s.stateHash;
                        found      = true;
                        break;
                    }
                }
                if (!found)
                    continue;

                if (m_currentGraph.entryStateHash == targetHash)
                    m_currentGraph.entryStateHash = 0;

                m_currentGraph.anyStateTransitions.erase(
                    std::remove_if(m_currentGraph.anyStateTransitions.begin(), m_currentGraph.anyStateTransitions.end(),
                                   [targetHash](const AnimTransition &t) { return t.targetStateHash == targetHash; }),
                    m_currentGraph.anyStateTransitions.end());

                for (auto &s : m_currentGraph.states)
                    s.transitions.erase(
                        std::remove_if(s.transitions.begin(), s.transitions.end(),
                                       [targetHash](const AnimTransition &t) { return t.targetStateHash == targetHash; }),
                        s.transitions.end());

                m_currentGraph.states.erase(
                    std::remove_if(m_currentGraph.states.begin(), m_currentGraph.states.end(),
                                   [targetHash](const AnimState &s) { return s.stateHash == targetHash; }),
                    m_currentGraph.states.end());
            }
        }
    }
    ed::EndDelete();
}



// リンク ID から遷移の素性を引く。このフレームの対応表に無ければ nullptr
const AnimGraphWindow::LinkRef *AnimGraphWindow::FindLink(int linkId) const
{
    for (const auto &[id, ref] : m_linkLut)
        if (id == linkId)
            return &ref;
    return nullptr;
}



// ---------------------------------------------------------------------------
// インスペクタ（ステート・遷移の設定）
// ---------------------------------------------------------------------------

// 選んでいるノードかリンクの設定を描く
void AnimGraphWindow::DrawInspector()
{
    ImGui::Text("インスペクタ (Inspector)");
    ImGui::Separator();

    // 選択が別へ移ったら、入力途中のステート名を捨てずに確定する。
    //   入力欄はその時点で描かれなくなるので、フォーカスが外れた合図を受け取れない
    if (m_stateNameEdited && StateNodeId(m_stateNameHash) != m_selectedNodeId) CommitStateName();

    if (m_selectedNodeId != 0)
        DrawStateSettings(m_selectedNodeId);
    else if (m_selectedLinkId != 0)
        DrawTransitionSettings(m_selectedLinkId);
    else
        ImGui::TextDisabled("項目を選択してください。\n(Select a Node or Link)");
}



// ステート（または Entry / AnyState）の設定を描く
void AnimGraphWindow::DrawStateSettings(int nodeId)
{
    AnimStateGraph &m_currentGraph = CurrentView(); // 現在の階層のステートを対象にする

    if (nodeId == ENTRY_NODE_ID)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "開始地点 (Entry Node)");
        ImGui::Separator();
        ImGui::TextDisabled("最初に再生するステートは、そのステートを\n右クリック → Set as Entry で指定します。");
        return;
    }
    if (nodeId == ANY_STATE_NODE_ID)
    {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.8f, 1.0f), "特別ノード (Any State)");
        ImGui::Separator();
        ImGui::TextDisabled("どのステートからでも遷移可能な特別遷移の起点です。\nこのノードを右クリック → Make Transition で、\n遷移先のステートをクリックします。");
        return;
    }

    for (auto &state : m_currentGraph.states)
    {
        if (StateNodeId(state.stateHash) != nodeId)
            continue;

        // 名前は確定（Enter・フォーカスを外す）したときだけ反映する。
        //   1文字ごとに反映すると、入力途中の名前ごとにハッシュを取り直してしまう
        if (m_stateNameHash != state.stateHash)
        {
            strncpy_s(m_stateNameBuf, sizeof(m_stateNameBuf), state.sequenceName.c_str(), _TRUNCATE);
            m_stateNameHash   = state.stateHash;
            m_stateNameEdited = false;
        }
        if (ImGui::InputText("状態名 (State Name)", m_stateNameBuf, sizeof(m_stateNameBuf))) m_stateNameEdited = true;
        if (ImGui::IsItemDeactivatedAfterEdit()) CommitStateName();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Enter かフォーカスを外すと確定します。\nコードから名前で参照しているステート（例: BackStepStop）は、\n変えたらコード側の名前も合わせてください。");

        ImGui::Separator();

        ImGui::Text("再生ファイル (Motion Sequence):");
        ImGui::TextDisabled("%s", state.sequenceFilePath.empty() ? "None" : state.sequenceFilePath.c_str());
        if (ImGui::Button("ファイルを選択 (Browse...)##Motion", ImVec2(-1, 0)))
        {
            if (auto path = FileDialog::OpenPreset("AnimSequence", "Select Anim Sequence"))
            {
                state.sequenceFilePath = FileDialog::ConvertToRelativePath(*path);
            }
        }

        ImGui::Separator();

        ImGui::Checkbox("ループ再生 (Loop Animation)", &state.isLoop);
        ImGui::Checkbox("ルートモーション適用 (Apply Root Motion)", &state.applyRootMotion);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("このステートで rootMotionCurve による前進を適用する。\nカーブは進捗→累積前進距離(m)。");
        ImGui::Checkbox("垂直方向もロック (Lock Vertical)", &state.lockVerticalRootMotion);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("ONにするとルートボーンのYも初期姿勢へ固定します。\n地上攻撃など浮いてほしくないステート向け。\nジャンプ攻撃など意図的な上下動があるステートはOFFのままにしてください。");
        ImGui::DragFloat("再生速度 (Playback Speed)", &state.playbackSpeed, 0.05f, 0.0f, 5.0f);

        ImGui::Separator();
        // グループ ID（軽量サブステートマシン）。
        //   AnyState の遷移の Source Group を同じ番号にすると、その群限定の割り込み遷移になる
        {
            int gid = (int)state.groupId;
            if (ImGui::InputInt("グループID (Group ID)", &gid))
                state.groupId = (uint32_t)(gid < 0 ? 0 : gid);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("軽量サブステートマシンのグループ番号 (0=無所属)。\n"
                                  "同じ番号のステート群に対し、AnyState の遷移で『Source Group』を\n"
                                  "同じ番号にすると、その群のどの状態からでも効く割り込みになります。\n"
                                  "例: Combat 群=1 にして『Source Group=1 → Idle [Finished]』を1本置く。");
        }

        ImGui::Separator();
        if (m_currentGraph.entryStateHash == state.stateHash)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "[ 初期状態 (Entry State) ]");
            ImGui::TextDisabled("※このノードが最初に再生されます。");
        }
        break;
    }
}



// 遷移の設定を描く（遷移はリンク ID から対応表で引き直す）
void AnimGraphWindow::DrawTransitionSettings(int linkId)
{
    AnimStateGraph &m_currentGraph = CurrentView(); // 現在の階層の遷移を対象にする

    if (linkId == ENTRY_LINK_ID)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "初期状態への遷移 (Entry Transition)");
        ImGui::Separator();
        ImGui::TextDisabled("※これはゲーム開始時に無条件で遷移するため、\n条件(Condition)を設定することはできません。");
        return;
    }

    const LinkRef *ref = FindLink(linkId);
    if (!ref)
        return;

    // ポインタを持ち越さず、ハッシュから今の遷移を引き直す
    AnimTransition *trans    = nullptr;
    std::string     fromName = "Unknown", toName = "Unknown";

    if (ref->isAnyState)
    {
        fromName = "Any State";
        for (auto &t : m_currentGraph.anyStateTransitions)
            if (t.targetStateHash == ref->dstHash)
            {
                trans = &t;
                break;
            }
    }
    else
    {
        for (auto &s : m_currentGraph.states)
        {
            if (s.stateHash == ref->srcHash)
            {
                fromName = s.sequenceName;
                for (auto &t : s.transitions)
                    if (t.targetStateHash == ref->dstHash)
                    {
                        trans = &t;
                        break;
                    }
                break;
            }
        }
    }
    if (!trans)
        return;

    for (const auto &s : m_currentGraph.states)
        if (s.stateHash == ref->dstHash)
        {
            toName = s.sequenceName;
            break;
        }

    if (ref->isAnyState)
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.8f, 1.0f), "特別遷移 (Any State Transition)");
    ImGui::Text("遷移元 (From): %s", fromName.c_str());
    ImGui::Text("遷移先 (To): %s", toName.c_str());
    ImGui::Separator();

    // AnyState の遷移だけ、グループ限定（Source Group）を指定できる。
    //   0 ならどこからでも、0 以外なら今のステートの Group ID が一致するときだけ有効
    if (ref->isAnyState)
    {
        int sg = (int)trans->sourceGroupId;
        if (ImGui::InputInt("スコープ限定 (Source Group)", &sg))
            trans->sourceGroupId = (uint32_t)(sg < 0 ? 0 : sg);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("0=どこからでも (従来の Any State)。\n"
                              "非0にすると、現在ステートの Group ID がこの番号と一致する時だけ有効な\n"
                              "『そのグループ限定の割り込み遷移』になります。");
        ImGui::Separator();
    }

    // 通常の遷移も Any State の遷移も同じ UI で編集する
    DrawTransitionConditionUI(*trans);
}



// ---------------------------------------------------------------------------
// ツールバーとファイル
// ---------------------------------------------------------------------------

// 保存・読込・全表示・テキスト出力のボタンを描く
void AnimGraphWindow::DrawToolbar()
{
    if (ImGui::Button("保存 (Save As...)"))
    {
        if (auto path = FileDialog::SavePreset("AnimNode", "Save Anim Graph"))
        {
            m_currentFilePath = FileDialog::ConvertToRelativePath(*path);
            SaveGraph(m_currentFilePath);
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("読込 (Load...)"))
    {
        if (auto path = FileDialog::OpenPreset("AnimNode", "Select Anim Graph File"))
        {
            m_currentFilePath = FileDialog::ConvertToRelativePath(*path);
            LoadGraph(m_currentFilePath);
        }
    }

    ImGui::SameLine();
    ImGui::TextDisabled("File: %s", m_currentFilePath.empty() ? "None" : m_currentFilePath.c_str());

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    if (ImGui::Button("全表示 (Focus All)"))
        ed::NavigateToContent();

    ImGui::SameLine();

    // --- テキスト出力 ---
    if (ImGui::Button("テキスト出力 (Export Text)"))
    {
        try
        {
            // 出力先は Build/Logs（ログと同じ場所）
            std::filesystem::create_directories("Build/Logs");

            // ファイル名が未設定（保存前）なら "Untitled_AnimGraph" を使う
            std::string filename = m_currentFilePath.empty()
                                       ? "Untitled_AnimGraph"
                                       : std::filesystem::path(m_currentFilePath).stem().string();

            std::string txtPath  = "Build/Logs/" + filename + ".txt";
            std::string dumpText = DumpAnimGraphToString(m_rootGraph); // サブマシンの中まで書き出す

            std::ofstream txtOut(txtPath);
            if (txtOut.is_open())
            {
                txtOut << dumpText;
                CCL_LOG_INFO(LogCategory::Editor, "[Anim Editor] Exported text dump to %s", txtPath.c_str());
            }
            else
            {
                CCL_LOG_ERROR(LogCategory::Editor, "[Anim Editor] Failed to open file for text dump: %s", txtPath.c_str());
            }
        }
        // CCL_LOG_WARN は Shipping で ((void)0) に潰れ、e が未使用になる。
        //   maybe_unused を付けて、構成によって警告が出ないようにする
        catch ([[maybe_unused]] const std::exception &e)
        {
            CCL_LOG_WARN(LogCategory::Editor, "[Anim Editor] Failed to export text dump: %s", e.what());
        }
    }
}



// グラフ全体（サブマシンの中身も含む）を JSON に保存する
void AnimGraphWindow::SaveGraph(const std::string &path)
{
    // 保存ボタンは入力欄より先に描くので、押したフレームではまだ名前の確定が届いていない
    CommitStateName();

    // 他の階層の位置は、階層を移るときに書き戻し済み
    WriteBackNodePositions();

    // 書き出すのはツリー全体（root）。サブマシンの中身も再帰的に含まれる
    if (AnimGraphSerializer::SaveToJSON(m_rootGraph, path))
        CCL_LOG_SUCCESS(LogCategory::Editor, "アニメグラフを保存しました: %s", path.c_str());
    else
        CCL_LOG_ERROR(LogCategory::Editor, "アニメグラフの保存に失敗しました: %s", path.c_str());
}



// JSON からグラフを読み込み、root の表示から始める
void AnimGraphWindow::LoadGraph(const std::string &path)
{
    if (AnimGraphSerializer::LoadFromJSON(m_rootGraph, path))
    {
        ed::ClearSelection();
        m_editPath.clear(); // 読込後は root 表示から始める

        // 入力途中の名前は前のグラフのもの。
        //   確定させると、読んだグラフの同じハッシュのステートを改名してしまう
        m_stateNameEdited = false;
        m_stateNameHash   = 0;

        m_needRestorePositions = true; // 次のフレームでノードの位置を復元する
    }
}



// ---------------------------------------------------------------------------
// 右クリックメニュー
// ---------------------------------------------------------------------------

// 右クリックメニュー（ノード上ならそのノードの操作、背景ならステートの追加）を出す
void AnimGraphWindow::HandleContextMenu()
{
    // 処理フロー:
    // 1. どこで右クリックしたかを見て、メニューを開く
    // 2. ノードのメニュー（遷移の作成・Entry 指定・複製・削除）
    // 3. 背景のメニュー（ステート・サブマシンの追加）

    AnimStateGraph &m_currentGraph = CurrentView(); // 現在の階層に対して追加・削除する

    ed::Suspend();

    // ---------------------------------------------------------
    // 1. メニューを開く
    // ---------------------------------------------------------

    // 開いたノードはポップアップが閉じるまで覚えておく（次のフレーム以降も同じノードを操作する）
    static int s_contextNodeId = 0;
    ed::NodeId contextNodeId;

    if (ed::ShowNodeContextMenu(&contextNodeId))
    {
        ImGui::OpenPopup("NodeContextMenu");
        s_contextNodeId = (int)(intptr_t)contextNodeId.Get();
    }
    else if (ed::ShowBackgroundContextMenu())
    {
        ImGui::OpenPopup("AnimCanvasContextMenu");
        m_newNodeSpawnPos = ImGui::GetMousePosOnOpeningCurrentPopup();
    }

    // ---------------------------------------------------------
    // 2. ノードのメニュー
    // ---------------------------------------------------------
    if (ImGui::BeginPopup("NodeContextMenu"))
    {
        // どのノードを操作しているかを、先頭にステート名で出す
        uint32_t    ctxHash = 0;
        const char *ctxName = "Node";
        if (s_contextNodeId == ENTRY_NODE_ID)
            ctxName = "Entry";
        else if (s_contextNodeId == ANY_STATE_NODE_ID)
            ctxName = "Any State";
        else
        {
            for (const auto &s : m_currentGraph.states)
                if (StateNodeId(s.stateHash) == s_contextNodeId)
                {
                    ctxHash = s.stateHash;
                    ctxName = s.sequenceName.c_str();
                    break;
                }
        }
        ImGui::TextDisabled("%s", ctxName);
        ImGui::Separator();

        // --- 遷移 ---
        if (s_contextNodeId != ENTRY_NODE_ID)
        {
            if (ImGui::MenuItem("Make Transition"))
            {
                m_editorMode = EditorMode::CreatingTransition;
                if (s_contextNodeId == ANY_STATE_NODE_ID)
                {
                    m_isTransitionSourceAnyState = true;
                    m_transitionSourceHash       = 0;
                }
                else
                {
                    m_isTransitionSourceAnyState = false;
                    m_transitionSourceHash       = ctxHash;
                }
            }
        }

        // --- ステート操作（通常のノードだけ。Entry / AnyState は対象外）---
        if (ctxHash != 0)
        {
            if (ImGui::MenuItem("Set as Entry"))
            {
                m_currentGraph.entryStateHash = ctxHash;
            }

            if (ImGui::MenuItem("Duplicate"))
            {
                // 「名前 + _Copy」で複製する。同じ名前が既にあれば "_Copy_2" などにずらす
                for (const auto &s : m_currentGraph.states)
                {
                    if (s.stateHash == ctxHash)
                    {
                        AnimState copy = s;
                        copy.sequenceName += "_Copy";
                        copy.stateHash = MakeUniqueStateHash(copy.sequenceName);
                        copy.nodePosX += 30.0f;
                        copy.nodePosY += 30.0f;
                        copy.transitions.clear(); // 遷移は引き継がない
                        m_currentGraph.states.push_back(copy);
                        m_nodeToPlaceHash = copy.stateHash;
                        break;
                    }
                }
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Delete"))
            {
                // このノードへ入る遷移を消してから、ノードを消す（出る遷移はノードと一緒に消える）
                for (auto &s : m_currentGraph.states)
                    s.transitions.erase(
                        std::remove_if(s.transitions.begin(), s.transitions.end(),
                                       [ctxHash](const AnimTransition &t) { return t.targetStateHash == ctxHash; }),
                        s.transitions.end());
                m_currentGraph.anyStateTransitions.erase(
                    std::remove_if(m_currentGraph.anyStateTransitions.begin(), m_currentGraph.anyStateTransitions.end(),
                                   [ctxHash](const AnimTransition &t) { return t.targetStateHash == ctxHash; }),
                    m_currentGraph.anyStateTransitions.end());
                if (m_currentGraph.entryStateHash == ctxHash)
                    m_currentGraph.entryStateHash = 0;

                m_currentGraph.states.erase(
                    std::remove_if(m_currentGraph.states.begin(), m_currentGraph.states.end(),
                                   [ctxHash](const AnimState &s) { return s.stateHash == ctxHash; }),
                    m_currentGraph.states.end());

                if (m_selectedNodeId == s_contextNodeId)
                    m_selectedNodeId = 0;
            }
        }

        ImGui::EndPopup();
    }

    // ---------------------------------------------------------
    // 3. 背景のメニュー
    // ---------------------------------------------------------
    if (ImGui::BeginPopup("AnimCanvasContextMenu"))
    {
        ImGui::Text("キャンバス操作 (Canvas Actions)");
        ImGui::Separator();

        ImGui::SetNextItemWidth(150);
        ImGui::InputText("##NewStateNameCtx", m_newStateName, sizeof(m_newStateName));
        ImGui::SameLine();

        if (ImGui::Button("新しいステートを追加") && m_newStateName[0] != '\0')
        {
            std::string nm = m_newStateName;
            AnimState   newState;
            newState.stateHash    = MakeUniqueStateHash(nm); // 階層全体で一意化
            newState.sequenceName = nm;

            ImVec2 canvasPos  = ed::ScreenToCanvas(m_newNodeSpawnPos);
            newState.nodePosX = canvasPos.x;
            newState.nodePosY = canvasPos.y;

            m_currentGraph.states.push_back(std::move(newState));
            // この階層にまだ Entry が無ければ、最初に置いたステートを Entry にする。
            //   新しいサブマシンは Entry が無いので、これが無いと入っても何も再生されない
            if (m_currentGraph.entryStateHash == 0)
                m_currentGraph.entryStateHash = m_currentGraph.states.back().stateHash;
            m_nodeToPlaceHash = m_currentGraph.states.back().stateHash;
            m_newStateName[0] = '\0';
            ImGui::CloseCurrentPopup();
        }

        // サブステートマシンを追加する（中身は空の子グラフ）。
        //   追加後、そのノードをダブルクリックで潜って中身を作る
        if (ImGui::Button("サブステートマシンを追加") && m_newStateName[0] != '\0')
        {
            std::string nm = m_newStateName;
            AnimState   sub;
            sub.stateHash    = MakeUniqueStateHash(nm);
            sub.sequenceName = nm;
            sub.isSubMachine = true;
            sub.subGraph     = std::make_unique<AnimStateGraph>(); // 空の子グラフ

            ImVec2 canvasPos = ed::ScreenToCanvas(m_newNodeSpawnPos);
            sub.nodePosX     = canvasPos.x;
            sub.nodePosY     = canvasPos.y;

            m_currentGraph.states.push_back(std::move(sub));
            // この階層にまだ Entry が無ければ、このサブマシンを Entry にする（root が空の場合など）
            if (m_currentGraph.entryStateHash == 0)
                m_currentGraph.entryStateHash = m_currentGraph.states.back().stateHash;
            m_nodeToPlaceHash = m_currentGraph.states.back().stateHash;
            m_newStateName[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ed::Resume();
}



// ---------------------------------------------------------------------------
// 遷移の条件
// ---------------------------------------------------------------------------

// 遷移の設定（ブレンド時間・受付区間・AND/OR・条件の一覧）を描く
void AnimGraphWindow::DrawTransitionConditionUI(AnimTransition &trans)
{
    // 処理フロー:
    // 1. ブレンド時間
    // 2. 遷移を許可する受付区間（requiredCancelMask）
    // 3. 条件の結合方法（AND / OR）
    // 4. 条件の一覧（対象の選択と、型に合わせた入力欄）

    // パラメータはステートマシン全体で共有するので、選択肢は常に root から出す
    AnimStateGraph &m_currentGraph = m_rootGraph;

    // ---------------------------------------------------------
    // 1. ブレンド時間
    // ---------------------------------------------------------

    ImGui::Text("遷移設定 (Transition Settings)");
    ImGui::DragFloat("ブレンド時間 (Blend Duration)", &trans.blendDuration, 0.01f, 0.0f, 2.0f, "%.2f s");
    ImGui::Separator();

    // ---------------------------------------------------------
    // 2. 受付区間
    // ---------------------------------------------------------
    ImGui::Text("遷移を許可する窓 (Required Cancel Mask):");
    ImGui::TextDisabled("※複数チェック時は「いずれかの窓」で許可されます。");

    // 選んでいる受付区間の名前をつなげて、コンボボックスの表示にする
    std::string previewMask = "";
    if (trans.requiredCancelMask == 0)
    {
        // 全て OFF（0）なら常に許可
        previewMask = "[ 常時遷移可能 (None) ]";
    }
    else
    {
        for (const auto &def : GetCancelFlagDefs())
        {
            if (trans.requiredCancelMask & def.flag)
            {
                if (!previewMask.empty()) previewMask += ", ";
                previewMask += def.name;
            }
        }
    }

    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##ReqCancelMask", previewMask.c_str()))
    {
        for (const auto &def : GetCancelFlagDefs())
        {
            bool active = (trans.requiredCancelMask & def.flag) != 0;
            if (ImGui::Checkbox(def.name, &active))
            {
                if (active)
                    trans.requiredCancelMask |= def.flag;
                else
                    trans.requiredCancelMask &= ~def.flag;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::Separator();

    // ---------------------------------------------------------
    // 3. 結合方法
    // ---------------------------------------------------------
    int         logicOp    = (int)trans.logicType;
    const char *logicOps[] = {"すべて満たす (AND)", "いずれかを満たす (OR)"};
    if (ImGui::Combo("条件判定方法 (Logic)", &logicOp, logicOps, IM_ARRAYSIZE(logicOps)))
        trans.logicType = (AnimTransitionLogic)logicOp;
    ImGui::Separator();

    // ---------------------------------------------------------
    // 4. 条件の一覧
    // ---------------------------------------------------------
    ImGui::Text("遷移条件 (Conditions)");
    if (ImGui::Button("+ 条件を追加 (Add Condition)"))
    {
        // 追加した直後は「再生終了」にしておく（パラメータ未選択のままでも判定できる）
        trans.conditions.push_back({0, "", AnimConditionOp::Finished, 0.0f});
    }

    // 削除はループの後で行う（ループ中に erase すると添字がずれる）
    int conditionToDelete = -1;

    for (size_t c = 0; c < trans.conditions.size(); ++c)
    {
        auto &cond = trans.conditions[c];

        ImGui::PushID((int)c);
        ImGui::Spacing();

        // --- 対象の選択 ---
        std::string preview = "未選択";
        if (cond.op == AnimConditionOp::Finished)
        {
            preview = "[ 再生終了 (Finished) ]";
        }
        else if (!cond.paramName.empty())
        {
            preview = cond.paramName;
        }

        ImGui::SetNextItemWidth(160);
        if (ImGui::BeginCombo("##Target", preview.c_str()))
        {
            // パラメータではない特別な項目
            bool isSelFinished = (cond.op == AnimConditionOp::Finished);
            if (ImGui::Selectable("[ 再生終了 (Finished) ]", isSelFinished))
            {
                cond.op        = AnimConditionOp::Finished;
                cond.paramHash = 0;
                cond.paramName = "";
            }

            ImGui::Separator();

            if (m_currentGraph.parameters.empty())
                ImGui::TextDisabled("パラメータ未登録");

            for (const auto &pd : m_currentGraph.parameters)
            {
                bool sel = (cond.paramHash == pd.hash && cond.op != AnimConditionOp::Finished);
                if (ImGui::Selectable(pd.name.c_str(), sel))
                {
                    cond.paramName = pd.name;
                    cond.paramHash = pd.hash;
                    // 型に合わせて演算子の初期値を決める
                    if (pd.type == AnimParamType::Trigger)
                        cond.op = AnimConditionOp::Trigger;
                    else if (pd.type == AnimParamType::Bool)
                        cond.op = AnimConditionOp::Equal;
                    else
                        cond.op = AnimConditionOp::Greater;
                }
                if (sel)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();

        // --- 型に合わせた入力欄 ---
        if (cond.op == AnimConditionOp::Finished)
        {
            // Finished の場合は演算子も閾値も不要
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "最後まで再生で遷移");
        }
        else if (cond.paramHash != 0)
        {
            // 対象パラメータの型を引く
            AnimParamType pType = AnimParamType::Float;
            for (const auto &pd : m_currentGraph.parameters)
            {
                if (pd.hash == cond.paramHash)
                {
                    pType = pd.type;
                    break;
                }
            }

            if (pType == AnimParamType::Trigger)
            {
                // Trigger は演算子も閾値も不要
                cond.op = AnimConditionOp::Trigger;
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "発動で遷移");
            }
            else if (pType == AnimParamType::Bool)
            {
                // Bool は == に固定し、true / false を選ばせる
                cond.op = AnimConditionOp::Equal;
                ImGui::Text(" は ");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(80);

                int         boolVal       = (cond.threshold > 0.5f) ? 1 : 0;
                const char *boolOptions[] = {"false", "true"};
                if (ImGui::Combo("##BoolVal", &boolVal, boolOptions, 2))
                {
                    cond.threshold = boolVal ? 1.0f : 0.0f;
                }
            }
            else if (pType == AnimParamType::Float)
            {
                // Float では Trigger / Finished を選べないようにする
                if (cond.op == AnimConditionOp::Trigger || cond.op == AnimConditionOp::Finished)
                    cond.op = AnimConditionOp::Greater;

                int         opInt      = (int)cond.op; // 0:Equal, 1:Greater, 2:Less
                const char *opsFloat[] = {"==", ">", "<"};

                ImGui::SetNextItemWidth(50);
                if (ImGui::Combo("##FloatOp", &opInt, opsFloat, 3))
                    cond.op = (AnimConditionOp)opInt;

                ImGui::SameLine();
                ImGui::SetNextItemWidth(60);
                ImGui::DragFloat("##FloatVal", &cond.threshold, 0.05f);
            }
        }
        else
        {
            ImGui::TextDisabled("対象を選択...");
        }

        // 行の右端の削除ボタン
        ImGui::SameLine();
        if (ImGui::Button("X", ImVec2(24, 0)))
        {
            conditionToDelete = (int)c;
        }

        ImGui::PopID();
    }

    if (conditionToDelete >= 0)
    {
        trans.conditions.erase(trans.conditions.begin() + conditionToDelete);
    }
}



// ---------------------------------------------------------------------------
// パラメータパネル（Unity Animator の Parameters タブ相当）
// ---------------------------------------------------------------------------

// root が持つ AnimParamDef を一覧で編集し、選択中のエンティティの今の値も並べて出す
void AnimGraphWindow::DrawParameterPanel(EditorContext &context)
{
    // 処理フロー:
    // 1. 型ごとの追加ボタン
    // 2. 一覧（名前・初期値・今の値・削除）
    // 3. 削除の反映（参照している条件も消す）

    // パラメータはステートマシン全体で共有（root だけが持つ）
    AnimStateGraph &m_currentGraph = m_rootGraph;

    ImGui::Text("パラメータ (Parameters)");
    ImGui::SameLine();

    // ---------------------------------------------------------
    // 1. 追加ボタン
    // ---------------------------------------------------------

    // 2 回押しても同じ名前（＝同じハッシュ）にならないよう、連番を付ける
    if (ImGui::SmallButton("+ Float"))
    {
        AnimParamDef p;
        p.name = MakeUniqueParamName("NewFloat");
        p.type = AnimParamType::Float;
        p.hash = CCL::Utils::HashString(p.name.c_str());
        m_currentGraph.parameters.push_back(p);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Bool"))
    {
        AnimParamDef p;
        p.name = MakeUniqueParamName("NewBool");
        p.type = AnimParamType::Bool;
        p.hash = CCL::Utils::HashString(p.name.c_str());
        m_currentGraph.parameters.push_back(p);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Trigger"))
    {
        AnimParamDef p;
        p.name = MakeUniqueParamName("NewTrigger");
        p.type = AnimParamType::Trigger;
        p.hash = CCL::Utils::HashString(p.name.c_str());
        m_currentGraph.parameters.push_back(p);
    }

    ImGui::Separator();

    // ---------------------------------------------------------
    // 2. 一覧
    // ---------------------------------------------------------

    // 今の値は、選択中のエンティティの AnimParametersComponent から読む
    AnimParametersComponent *liveParams = nullptr;
    if (context.selectedEntity != CCL::ECS::InvalidEntityID && context.world)
        liveParams = context.world->GetComponent<AnimParametersComponent>(context.selectedEntity);

    if (!ImGui::BeginTable("ParamTable", 4,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY))
        return;

    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Type/Default", ImGuiTableColumnFlags_WidthFixed, 120.0f);
    ImGui::TableSetupColumn("Live", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 28.0f); // 削除ボタン列
    ImGui::TableHeadersRow();

    int deleteIndex = -1;

    for (int i = 0; i < (int)m_currentGraph.parameters.size(); ++i)
    {
        AnimParamDef &p = m_currentGraph.parameters[i];
        ImGui::PushID(i);
        ImGui::TableNextRow();

        // --- 列 1: 名前（ダブルクリックで改名） ---
        ImGui::TableSetColumnIndex(0);
        if (m_paramRenameIndex == i)
        {
            ImGui::SetNextItemWidth(-1);
            bool commit = ImGui::InputText("##rename", m_paramNameBuf, sizeof(m_paramNameBuf),
                                           ImGuiInputTextFlags_EnterReturnsTrue);
            if (commit || ImGui::IsItemDeactivatedAfterEdit())
            {
                std::string newName = m_paramNameBuf;
                if (!newName.empty() && newName != p.name)
                {
                    // 既存の名前と重なるなら改名しない。
                    //   同じハッシュの定義が 2 つになり、片方を消すともう片方を使う条件まで消える
                    if (MakeUniqueParamName(newName) != newName)
                    {
                        CCL_LOG_WARN(LogCategory::Editor, "[Anim Editor] パラメータ名 '%s' は既にあります", newName.c_str());
                    }
                    else
                    {
                        uint32_t oldHash = p.hash;
                        uint32_t newHash = CCL::Utils::HashString(newName.c_str());
                        // 参照している全条件を張り替える（怠ると、条件が消えたパラメータを指す）
                        RenameParameterReferences(oldHash, newName, newHash);
                        p.name = newName;
                        p.hash = newHash;
                    }
                }
                m_paramRenameIndex = -1;
            }
        }
        else
        {
            // 型ごとに色を分ける
            ImVec4 col = (p.type == AnimParamType::Float)  ? ImVec4(0.5f, 0.8f, 1.0f, 1.0f)
                         : (p.type == AnimParamType::Bool) ? ImVec4(0.6f, 1.0f, 0.6f, 1.0f)
                                                           : ImVec4(1.0f, 0.8f, 0.4f, 1.0f);
            ImGui::TextColored(col, "%s", p.name.c_str());
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
            {
                m_paramRenameIndex = i;
                strncpy_s(m_paramNameBuf, sizeof(m_paramNameBuf), p.name.c_str(), _TRUNCATE);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("ID: 0x%08X\n(ダブルクリックでリネーム)", p.hash);
        }

        // --- 列 2: 型と初期値 ---
        ImGui::TableSetColumnIndex(1);
        switch (p.type)
        {
        case AnimParamType::Float:
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat("##def", &p.defaultFloat, 0.05f);
            break;
        case AnimParamType::Bool:
            ImGui::Checkbox("##def", &p.defaultBool);
            ImGui::SameLine();
            ImGui::TextDisabled("Bool");
            break;
        case AnimParamType::Trigger:
            ImGui::TextDisabled("Trigger");
            break;
        }

        // --- 列 3: 今の値 ---
        ImGui::TableSetColumnIndex(2);
        if (liveParams && p.type == AnimParamType::Trigger)
        {
            // トリガーは Float / Bool とは別の、寿命付きの一覧に入っている
            const bool on = liveParams->HasTrigger(p.hash);
            ImGui::TextColored(on ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                               on ? "TRUE" : "false");
        }
        else if (liveParams)
        {
            float fv = 0.0f;
            if (liveParams->TryGetNumeric(p.hash, fv))
            {
                if (p.type == AnimParamType::Bool)
                    ImGui::TextColored(fv != 0.0f ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                                       fv != 0.0f ? "TRUE" : "false");
                else
                    ImGui::Text("%.2f", fv);
            }
            else
                ImGui::TextDisabled("-");
        }
        else
            ImGui::TextDisabled("-");

        // --- 列 4: 削除ボタン ---
        ImGui::TableSetColumnIndex(3);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.60f, 0.20f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
        if (ImGui::SmallButton("×"))
            deleteIndex = i;
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("このパラメータを削除");

        // 右クリックでも削除メニューを出す
        if (ImGui::BeginPopupContextItem("paramCtx"))
        {
            if (ImGui::MenuItem("削除 (Delete)"))
                deleteIndex = i;
            ImGui::EndPopup();
        }

        ImGui::PopID();
    }

    ImGui::EndTable();

    // ---------------------------------------------------------
    // 3. 削除の反映
    // ---------------------------------------------------------

    // ループの外で消す（ループ中に erase すると添字がずれる）
    if (deleteIndex >= 0)
    {
        // 消すパラメータを参照している遷移条件も一緒に消す。
        //   残すと、存在しないハッシュを参照する条件が残る
        const uint32_t deadHash   = m_currentGraph.parameters[deleteIndex].hash;
        auto           stripConds = [deadHash](std::vector<AnimTransition> &transitions) {
            for (auto &t : transitions)
                t.conditions.erase(
                    std::remove_if(t.conditions.begin(), t.conditions.end(),
                                   [deadHash](const AnimCondition &c) { return c.paramHash == deadHash; }),
                    t.conditions.end());
        };
        // 階層のどこにある条件も漏れなく消す（root ＋ 全サブグラフ）
        ForEachTransitionList(m_rootGraph, stripConds);

        m_currentGraph.parameters.erase(m_currentGraph.parameters.begin() + deleteIndex);
    }
}



// パラメータの改名に合わせて、全遷移・AnyState 遷移の条件の参照を張り替える
void AnimGraphWindow::RenameParameterReferences(uint32_t oldHash, const std::string &newName, uint32_t newHash)
{
    auto patch = [&](std::vector<AnimTransition> &transitions) {
        for (auto &t : transitions)
            for (auto &c : t.conditions)
                if (c.paramHash == oldHash)
                {
                    c.paramHash = newHash;
                    c.paramName = newName;
                }
    };
    // 階層のどこにある条件も漏れなく張り替える（root ＋ 全サブグラフ）
    ForEachTransitionList(m_rootGraph, patch);
}



// base を元に、既存のパラメータと重ならない名前を返す
std::string AnimGraphWindow::MakeUniqueParamName(const std::string &base) const
{
    // 重なりは名前ではなくハッシュで調べる（条件はハッシュでパラメータを引くため）
    auto IsTaken = [this](const std::string &name) {
        const uint32_t hash = CCL::Utils::HashString(name.c_str());
        for (const auto &p : m_rootGraph.parameters)
        {
            if (p.hash == hash) return true;
        }
        return false;
    };

    std::string name = base;
    for (int i = 2; IsTaken(name); ++i)
    {
        name = base + "_" + std::to_string(i);
    }
    return name;
}
