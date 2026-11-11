#pragma once
// ===========================================================================
// 概要: アニメーションのステートマシン（AnimStateGraph）をノードグラフで編集するウィンドウ
//
// 責務:
//   - ステート・遷移・サブステートマシン・ブレンドスペースの追加・削除と、遷移条件の編集
//   - パラメータ（Float / Bool / Trigger）の一覧編集と、実行中の値の表示
//   - 選択中のエンティティで実行中のステートと、直前の遷移を強調表示する
//   - JSON の読み書きと、構造のテキスト書き出し
//
// 備考: 編集対象は m_rootGraph（サブマシンの中身も含むツリー全体）。今見ている階層は
//       m_editPath で決まる。リンクの削除・選択は、毎フレーム作り直す m_linkLut
//       （linkId → 遷移の素性）で解決する
// ===========================================================================
#include "Editor/Core/EditorWindow.h"
#include "Engine/Animation/Core/AnimStateMachine.h"
#include "Engine/Animation/Core/AnimBlendSpaceMath.h"
#include <string>
#include <vector>
#include <array>
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
    // =====================================================================
    // 描画
    // =====================================================================
    void DrawToolbar();       // 保存・読込・全表示・テキスト出力のボタン
    // 今の階層のノードとリンクを描き、遷移の作成・削除・右クリックを処理する
    void DrawNodeWorkspace(); // 今の階層のノードとリンク
    void DrawInspector();     // 選んでいるノードかリンクの設定

    // ステート（または Entry / AnyState）の設定を描く。nodeId は ed のノード ID（予約の負値か hash * 4）
    void DrawStateSettings(int nodeId);                    // ステートの設定。nodeId は ed のノード ID（予約の負値か hash * 4）
    // 遷移の設定を描く。遷移はリンク ID から対応表で引き直す
    void DrawTransitionSettings(int linkId);               // 遷移の設定
    // 遷移の設定（ブレンド時間・受付区間・AND/OR・条件の一覧）を描く。通常の遷移と AnyState の遷移で共通
    void DrawTransitionConditionUI(AnimTransition &trans); // 遷移の条件（通常の遷移と AnyState の遷移で共通）

    // =====================================================================
    // パラメータ（Unity の Parameters タブ相当）
    // =====================================================================
    // root が持つ AnimParamDef を一覧で編集し、選択中のエンティティの今の値も並べて出す
    void DrawParameterPanel(EditorContext &context);

    // パラメータの改名に合わせて、全遷移・AnyState 遷移の条件の参照を張り替える
    void RenameParameterReferences(uint32_t oldHash, const std::string &newName, uint32_t newHash);

    // base を元に、既存のパラメータと重ならない名前を返す（"NewFloat" → "NewFloat_2" など）
    std::string MakeUniqueParamName(const std::string &base) const;

    // Float のパラメータを選ぶコンボ。選んだら hash と name を書き換えて true を返す
    bool DrawFloatParamCombo(const char *label, uint32_t &hash, std::string &name);

    // =====================================================================
    // ブレンドスペース
    // =====================================================================
    void DrawBlendSpaceSettings(AnimState &state);   // 種類・軸のパラメータ・追従の時間・点の一覧と、図
    void DrawBlendSpacePlot(const AnimState &state); // 点の配置と、入力・重みの図（実行中ならその入力、そうでなければドラッグで試す）

    // =====================================================================
    // 編集の操作
    // =====================================================================

    /**
     * @brief ステート名を変え、名前のハッシュを stateHash として取り直す
     *
     * ゲーム側は HashString("名前") でステートを引くので、名前とハッシュを常に一致させる
     * 同じ階層の遷移先・Entry・潜っている経路の参照も張り替える
     *
     * @param oldHash 変えるステート。どの階層にあってもよい
     * @param newName 新しい名前。既存の名前と重なれば連番を付けて一意にする
     */
    void RenameState(uint32_t oldHash, std::string newName);
    void CommitStateName(); // 入力途中のステート名を確定する

    // 今の階層のノード位置（ノードエディタ上のライブな値）をグラフへ書き戻す
    //   階層を移る前と保存の前に呼ぶ。呼ばずに移ると、戻ったとき動かす前の位置に戻る
    void WriteBackNodePositions();

    // 選択中のリンクを、前のフレームと同じ遷移へ付け直す（遷移の増減でリンク ID がずれるため）
    void SyncSelectedLink();

    // キャンバス上の操作を処理する（ダブルクリックでサブマシンへ潜る予約、リンク・ノードの削除）
    void HandleInteraction();
    // 右クリックメニュー（ノード上ならそのノードの操作、背景ならステートの追加）を出す
    void HandleContextMenu();

    void LoadGraph(const std::string &path); // JSON からグラフを読み込み、root の表示から始める
    void SaveGraph(const std::string &path); // グラフ全体（サブマシンの中身も含む）を JSON に保存する

    // =====================================================================
    // 階層ナビゲーション（サブステートマシンに潜る・戻る）
    //   各描画関数は冒頭で `AnimStateGraph &m_currentGraph = CurrentView();` と局所参照を取り、
    //   以降は今の階層だけを編集する（m_currentGraph はメンバではない）
    // =====================================================================
    AnimStateGraph &CurrentView();                            // m_editPath に沿って辿った、今編集している階層のグラフ
    void            DrawBreadcrumb();                         // 「Root > Atk_Combo > ...」のパンくず。クリックで浅い階層へ戻る
    // 指定のサブマシンへ潜る。今の階層になければ何もしない
    void            EnterSubMachine(uint32_t subMachineHash); // 指定のサブマシンへ潜る
    // 階層全体で重ならない stateHash を採る。重なれば name に "_2", "_3"... を付け、その名前のハッシュを返す
    uint32_t        MakeUniqueStateHash(std::string &name);   // 階層全体で重ならない hash を採る（重なれば name に連番を付ける）

    // =====================================================================
    // リンク ID → 遷移の素性の対応表（毎フレーム DrawNodeWorkspace で作り直す）
    //   遷移は (srcHash, dstHash) で一意（重複は作成時に弾いている）なので、
    //   ポインタを持ち越さず、ハッシュから毎回今の遷移を引き直す
    // =====================================================================
    struct LinkRef
    {
        uint32_t srcHash;
        uint32_t dstHash;
        bool     isAnyState;

        bool operator==(const LinkRef &) const = default;
    };
    std::vector<std::pair<int, LinkRef>> m_linkLut;
    const LinkRef                       *FindLink(int linkId) const; // リンク ID から遷移を引く。なければ nullptr

private:
    AnimStateGraph        m_rootGraph;                                              // ツリー全体（保存・読込の対象）
    std::vector<uint32_t> m_editPath;                                               // 潜っているサブマシンの hash 列（空なら root）
    std::string           m_currentFilePath = "Assets/Animations/PlayerAnimGraph.json"; // 保存・読込の対象のパス

    ax::NodeEditor::EditorContext *m_nodeContext = nullptr; // 所有する（デストラクタで破棄）

    // --- 選択（0 = 未選択）---
    int     m_selectedNodeId  = 0;  // ed のノード ID（予約の負値か hash * 4）
    int     m_selectedLinkId  = 0;  // ed のリンク ID
    LinkRef m_selectedLinkRef = {}; // m_selectedLinkId が指していた遷移（ID がずれたときの付け直し用）

    // --- ステート名の入力欄（確定するまでグラフへ反映しない）---
    char     m_stateNameBuf[64] = "";    // 入力中の名前
    uint32_t m_stateNameHash    = 0;     // 入力欄が表すステート（0 なら未使用）
    bool     m_stateNameEdited  = false; // 未確定の変更があるか

    // --- ノードの配置と、次のフレームへの予約 ---
    uint32_t m_nodeToPlaceHash      = 0;            // 次の描画で位置を置くノード（追加・複製・改名の直後。0 ならなし）
    bool     m_selectPlacedNode     = true;         // m_nodeToPlaceHash を置いたあと選択するか
    uint32_t m_pendingEnterSub      = 0;            // 次のフレームの頭で潜るサブマシン（0 ならなし）
    char     m_newStateName[64]     = "";           // 右クリックメニューで追加するステートの名前
    bool     m_needRestorePositions = false;        // 次の描画でノードの位置をグラフの値へ戻すか（読込・階層の移動の後）
    ImVec2   m_newNodeSpawnPos      = {0.0f, 0.0f}; // 右クリックした位置（スクリーン座標）

    uint32_t m_runtimeActiveStateHash = 0; // 選択中のエンティティで実行中のステート（強調表示に使う。0 ならなし）

    // --- ブレンドスペースの図 ---
    bool                                           m_runtimeBlendActive  = false; // 選択中のエンティティがブレンドスペースを再生中か
    float                                          m_runtimeBlendX       = 0.0f;  // その入力（目標へ寄せた後）
    float                                          m_runtimeBlendY       = 0.0f;
    std::array<float, CCL::Anim::kMaxBlendSamples> m_runtimeBlendWeights = {};    // その重み
    float                                          m_blendPreviewX       = 0.0f;  // 実行中でないときに、図をドラッグして試す入力
    float                                          m_blendPreviewY       = 1.0f;

    // --- パラメータの改名 ---
    int  m_paramRenameIndex = -1; // 改名中のパラメータの添字（-1 ならなし）
    char m_paramNameBuf[64] = ""; // 改名の入力

    // --- 遷移の作成（Unity の Make Transition と同じ操作）---
    enum class EditorMode
    {
        Normal,
        CreatingTransition // 遷移元を決め、遷移先のクリックを待っている
    };
    EditorMode m_editorMode                 = EditorMode::Normal;
    uint32_t   m_transitionSourceHash       = 0;     // 作成中の遷移の元
    bool       m_isTransitionSourceAnyState = false; // 作成中の遷移の元が AnyState か
};