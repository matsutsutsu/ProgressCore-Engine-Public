#pragma once
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

/// @brief アニメーションのステートマシン(AnimStateGraph)をノードグラフで編集するウィンドウ。
/// @details 編集対象は m_currentGraph。リンクの削除・選択は位置依存ではなく
///          毎フレーム再構築する m_linkLut(linkId→遷移の素性)で解決する。
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
    void DrawStateSettings(int nodeId); ///< 引数は ed のノードID(予約負値 or hash*4)
    void DrawTransitionSettings(int linkId);

    // Transition の共通UI描画
    void DrawTransitionConditionUI(AnimTransition &trans);

    // パラメータパネル（Unity の Parameters タブ相当）
    void DrawParameterPanel(EditorContext &context);
    /// @brief パラメータ名変更時、参照している全条件の paramHash/paramName を張り替える。
    void RenameParameterReferences(uint32_t oldHash, const std::string &newName, uint32_t newHash);

    void HandleInteraction();
    void HandleContextMenu();

    void LoadGraph(const std::string &path);
    void SaveGraph(const std::string &path);

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
    AnimStateGraph m_currentGraph;
    std::string    m_currentFilePath = "Assets/Animations/PlayerAnimGraph.json";

    ax::NodeEditor::EditorContext *m_nodeContext = nullptr;

    // 選択状態（0 = 未選択）
    int m_selectedNodeId = 0; ///< ed のノードID（予約負値 or hash*4）
    int m_selectedLinkId = 0; ///< ed のリンクID

    uint32_t m_nodeToPlaceHash  = 0;
    char     m_newStateName[64] = "";

    bool   m_needRestorePositions = false;
    ImVec2 m_newNodeSpawnPos      = {0.0f, 0.0f};

    // デバッグ用：現在実行中のステート
    uint32_t m_runtimeActiveStateHash = 0;

    int  m_paramRenameIndex = -1; ///< リネーム編集中のパラメータindex（-1=なし）
    char m_paramNameBuf[64] = ""; ///< リネーム入力バッファ

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