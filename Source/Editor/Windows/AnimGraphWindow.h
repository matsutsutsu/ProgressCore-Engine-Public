#pragma once
// ===========================================================================
// 概要: アニメーションのステートマシン（AnimStateGraph）をノードグラフで編集するウィンドウ。
//
// 責務:
//   - ステート・遷移・サブステートマシンの追加・削除と、遷移条件の編集
//   - パラメータ（Float / Bool / Trigger）の一覧編集と、実行中の値の表示
//   - 選択中のエンティティで実行中のステートと、直前の遷移を強調表示する
//   - JSON の読み書きと、構造のテキスト書き出し
//
// 備考: 編集対象は m_rootGraph（サブマシンの中身も含むツリー全体）。今見ている階層は
//       m_editPath で決まる。リンクの削除・選択は、毎フレーム作り直す m_linkLut
//       （linkId → 遷移の素性）で解決する。
// ===========================================================================
#include "Editor/Core/EditorWindow.h"
#include "Engine/Animation/Core/AnimStateMachine.h"
#include <string>
#include <vector>
#include <utility>
#include <cstdint>

namespace ax
{
    namespace NodeEditor
    {
        struct EditorContext;
    }
} // namespace ax

class AnimGraphWindow : public EditorWindow
{
public:
    AnimGraphWindow();
    virtual ~AnimGraphWindow();

protected:
    void DrawContents(EditorContext &context) override;

private:
    void DrawToolbar();
    void DrawNodeWorkspace();
    void DrawInspector();

    // インスペクタの描画を役割ごとに分割
    void DrawStateSettings(int nodeId); // 引数は ed のノードID(予約負値 or hash*4)
    void DrawTransitionSettings(int linkId);

    // Transition の共通UI描画
    void DrawTransitionConditionUI(AnimTransition &trans);

    // パラメータパネル（Unity の Parameters タブ相当）
    void DrawParameterPanel(EditorContext &context);
    // パラメータ名変更時、参照している全条件の paramHash/paramName を張り替える。
    void RenameParameterReferences(uint32_t oldHash, const std::string &newName, uint32_t newHash);

    void HandleInteraction();
    void HandleContextMenu();

    void LoadGraph(const std::string &path);
    void SaveGraph(const std::string &path);

    // =====================================================================
    // 階層ナビゲーション（サブステートマシンに潜る/戻る）
    //   m_rootGraph が保存対象のツリー全体。m_editPath が「今どのサブマシンに
    //   潜っているか」の hash 列（空=root表示）。CurrentView() は m_editPath に
    //   沿って m_rootGraph を辿り「今編集中の階層グラフ」への参照を返す。
    //   各描画メソッドは冒頭で `AnimStateGraph& m_currentGraph = CurrentView();`
    //   と局所参照を取り、以降は今の階層だけを編集する。
    // =====================================================================
    AnimStateGraph &CurrentView();
    void            DrawBreadcrumb();                         // 「Root > Combat > ...」パンくず。クリックで浅い階層へ戻る。
    void            EnterSubMachine(uint32_t subMachineHash); // 指定サブマシンへ潜る
    uint32_t        MakeUniqueStateHash(std::string &name);   // 階層全体で衝突しない hash を採る（name は必要なら連番付与で書き換え）

    // =====================================================================
    // リンクID → 遷移の素性 の対応表（毎フレーム DrawNodeWorkspace で再構築）
    //   削除・選択を「描画順の数え直し」ではなくこの表で解決することで、
    //   遷移を1本消しても後続リンクの対応がズレない。
    //   遷移は (srcHash,dstHash) で一意（重複遷移は作成時に弾いている）なので
    //   ポインタを保持せず、ハッシュから毎回ライブ参照を引き直す（ダングリング無し）。
    // =====================================================================
    struct LinkRef
    {
        uint32_t srcHash;
        uint32_t dstHash;
        bool     isAnyState;
    };
    std::vector<std::pair<int, LinkRef>> m_linkLut;
    const LinkRef                       *FindLink(int linkId) const;

private:
    AnimStateGraph        m_rootGraph; // ツリー全体（保存/読込の対象）
    std::vector<uint32_t> m_editPath;  // 潜っているサブマシンの hash 列（空=root）
    std::string           m_currentFilePath = "Assets/Animations/PlayerAnimGraph.json";

    ax::NodeEditor::EditorContext *m_nodeContext = nullptr;

    // 選択状態（0 = 未選択）
    int m_selectedNodeId = 0; // ed のノードID（予約負値 or hash*4）
    int m_selectedLinkId = 0; // ed のリンクID

    uint32_t m_nodeToPlaceHash  = 0;
    uint32_t m_pendingEnterSub  = 0; // このフレーム末に潜るサブマシン hash（0=なし。描画中の破壊回避で遅延実行）
    char     m_newStateName[64] = "";

    bool   m_needRestorePositions = false;
    ImVec2 m_newNodeSpawnPos      = {0.0f, 0.0f};

    // 選択中のエンティティで実行中のステート（強調表示に使う。0 なら無し）
    uint32_t m_runtimeActiveStateHash = 0;

    int  m_paramRenameIndex = -1; // リネーム編集中のパラメータindex（-1=なし）
    char m_paramNameBuf[64] = ""; // リネーム入力バッファ

    // =====================================================================
    // エディタの独自ステート管理 (Unity風 Make Transition用)
    // =====================================================================
    enum class EditorMode
    {
        Normal,
        CreatingTransition
    };
    EditorMode m_editorMode                 = EditorMode::Normal;
    uint32_t   m_transitionSourceHash       = 0;
    bool       m_isTransitionSourceAnyState = false;
};