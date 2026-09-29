// ビヘイビアツリーを視覚的に組み立てるエディタウィンドウ。
//
// 責務:
//   人が編集しやすい「ノードとリンクの網目」を持ち、保存時にランタイム用のフラット配列（Bake）へ
//   変換する。ランタイムの BT は配列しか知らないので、エディタ側の構造は自由に変えてよい。
//
// 守らないと壊れる規約:
//   - 子の実行順は「左が先。X が同じなら上が先」。RebuildHierarchy が1か所で決め、Bake・Priority 表示・
//     自動整列はその並びを使う。座標を動かす処理は、兄弟の左右関係を崩さないこと。
//   - 保存項目は NodeToJson / NodeFromJson の2か所だけに書く（保存・読込・Undo が全部ここを通る）。
//
// 実装は役割ごとに分けてある（同じクラスのメンバ関数を別 .cpp で定義）:
//   BehaviorTreeWindow.cpp            ウィンドウ枠・ツールバー・編集操作・Undo・表示名
//   BehaviorTreeWindow_Graph.cpp      保存・読込・Bake・親子関係と表示する階層（描画なし）
//   BehaviorTreeWindow_Layout.cpp     自動整列・コメント枠の配置
//   BehaviorTreeWindow_Canvas.cpp     キャンバスの描画と入力
//   BehaviorTreeWindow_Inspector.cpp  右ペイン：選択ノードの編集
//   BehaviorTreeWindow_Outliner.cpp   左ペイン：木の一覧
#pragma once
#include "Editor/Core/EditorWindow.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h"
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <json.hpp>

namespace ax
{
    namespace NodeEditor
    {
        struct EditorContext;
    }
} // namespace ax

struct BehaviorTreeComponent;

/// エディタ上のノードの種類。
//   editor_nodes は "type" を数値で保存するので、列挙子の位置を動かすと保存済みのノードが
//   別の種類になる。追加は必ず末尾へ。
enum class EditorBTNodeType
{
    Root,             // 始点（1グラフに1つ）。入力ピンを持たない
    Selector,         // 子を左から試し、1つ成功したら成功
    Sequence,         // 子を左から実行し、1つ失敗したら失敗
    Condition,        // 葉：条件を評価して真偽を返す
    Action,           // 葉：行動（攻撃・移動など）
    Group,            // コメント枠。実行には関わらない
    Decorator,        // 子1つの結果を修飾する（Inverter / Cooldown / Retry）
    WeightedSelector, // 子を重みで抽選する
    Parallel,         // 0番目の子が主、残りは従で同時に動かす
    SubTree           // 中に木を1本持つ箱。ダブルクリックで中へ入る。Bake で中身に置き換わる
};

/// ノードの接続口
struct BTPin
{
    uint32_t id;      // ピンのID（ノードエディタが識別に使う）
    uint32_t nodeId;  // 持ち主のノードID
    bool     isInput; // true=上の入力（親から）、false=下の出力（子へ）
};

/// ピンとピンを繋ぐ線。親の出力ピン → 子の入力ピン の向きで持つ
struct BTLink
{
    uint32_t id;         // リンクのID
    uint32_t startPinId; // 親の出力ピン
    uint32_t endPinId;   // 子の入力ピン
};

/// エディタ上のノード1個
struct EditorBTNode
{
    uint32_t         id;                      // ノードのID
    EditorBTNodeType type;                    // 種類
    std::string      name;                    // 表示名（コメント枠では見出し）
    ActionID         actionOrConditionId = 0; // Action / Condition が指す行動・条件のID
    BTPin            inputPin;                // 入力ピン（id=0 なら持たない）
    BTPin            outputPin;               // 出力ピン（id=0 なら持たない）

    float posX                = 0.0f;  // キャンバス座標（左上）
    float posY                = 0.0f;  // 同上
    bool  positionInitialized = false; // false の間、次の描画で posX/posY をライブラリへ流し込む
    int   executionOrder      = -1;    // 兄弟の中での実行順（1始まり）。Priority 表示用

    float width           = 400.0f; // コメント枠の枠の幅
    float height          = 400.0f; // コメント枠の枠の高さ
    int   groupColorIndex = 0;      // コメント枠の色（BTEditor::kGroupPalette の添字）
    int   decoratorType   = 0;      // デコレータの種類（0=Inverter, 1=Cooldown, 2=Retry）

    float param  = 0.0f; // 数値パラメータ。BTNode::param と1対1（待機秒数・クールダウン秒数など）
    float weight = 1.0f; // 重み付きセレクターの子としての選ばれやすさ。0 以下で候補外
    bool  interrupt = false; // Selector の子として、上の枝が動いていても割り込んで入るか
    bool  abortSelf = false; // Selector の子として、走っている間も自分の条件を見直し、崩れたら降りるか

    // どの階層に置かれているか。0 は一番外、それ以外はそのノードを中に持つ SubTree のID。
    //   サブツリーの中身も同じ m_nodes に入れておき、キャンバスには今の階層のものだけ描く。
    //   入れ子の構造体にしないのは、保存・Undo・複製などの既存処理を平らな配列のまま使えるため。
    uint32_t graphId = 0;

    int runtimeFlatIndex = -1; // Bake 後の配列での添字。実行中の状態表示の対応付けに使う
};

/// ビヘイビアツリーエディタのウィンドウ
class BehaviorTreeWindow : public EditorWindow
{
public:
    BehaviorTreeWindow();
    virtual ~BehaviorTreeWindow();

    /// 現在のグラフを JSON で返す（Undo/Redo の記録用）
    nlohmann::json GetGraphStateAsJson();

    /// JSON の状態へグラフを戻す（Undo/Redo 用）。選択は保つ
    void RestoreGraphState(const nlohmann::json &state);

protected:
    /// 3列（アウトライン｜キャンバス｜インスペクタ）を描く
    virtual void DrawContents(EditorContext &context) override;

private:
    // ---------------------------------------------------------------
    // BehaviorTreeWindow.cpp : ウィンドウ枠・編集操作・表示名
    // ---------------------------------------------------------------

    /// 保存・開く・整列などのボタン列
    void DrawToolbar();

    // 「一番外 > 近接 > ...」のパンくず。押した段の階層へ戻る
    void DrawBreadcrumb();

    /// 指定位置にノードを作る（ピンは種類に応じて発行する）。置く階層は今見ている階層。作ったノードのIDを返す
    uint32_t CreateNode(EditorBTNodeType type, const std::string &name, float x, float y);

    // 空のサブツリーを作る。中に入口（ROOT）を1つ置く。作った SubTree のIDを返す
    uint32_t CreateSubTreeNode(float x, float y);

    // 選択中のノードを1つのサブツリーへまとめる（UE の Blueprint の Collapse Nodes と同じ）。
    //   選択は「根が1つの枝」でなければならない（途中の親が選択外だと、どこへ繋ぎ直すか決まらない）。
    void CollapseSelectionToSubTree();

    // サブツリーの中へ入る。実際に切り替えるのは次のフレームの頭（描画中に階層を変えないため）
    void RequestEnterSubTree(uint32_t subTreeId) { m_pendingEnterId = subTreeId; }

    // 今見ている階層（0 は一番外）
    uint32_t CurrentGraphId() const { return m_viewPath.empty() ? 0 : m_viewPath.back(); }

    // subTreeId の中に（入れ子も含めて）置かれているノードのID
    std::vector<uint32_t> CollectSubTreeContents(uint32_t subTreeId) const;

    // サブツリーの入口（中の ROOT）。無ければ nullptr
    EditorBTNode *FindSubTreeEntry(uint32_t subTreeId);

    /// ノードと、それに繋がるリンクを消す
    void DeleteNodeAndLinks(uint32_t nodeId);

    /// 選択中のノードと、その間のリンクを複製する（コピーして右下へずらして貼るのと同じ）
    void DuplicateSelectedNodes();

    // 選択中のノードを OS のクリップボードへ JSON で書く（Ctrl+C）。別の BT ファイルへも貼れる
    void CopySelection();

    // コピーしてから選択中のノードを消す（Ctrl+X）
    void CutSelection();

    // クリップボードの内容を canvasPos に貼る（Ctrl+V）。このエディタの形式でなければ何もしない
    void PasteClipboard(ImVec2 canvasPos);

    /**
     * @brief 選択中のノードを、貼り付けに使える JSON にする。選択が空なら null。
     *
     * 選んだ SubTree は中身ごと入れる（中身が無いと入口も無く動かない）。
     * リンクは、両端がコピーに含まれるものだけを入れる。
     */
    nlohmann::json CopySelectionToJson();

    /**
     * @brief CopySelectionToJson の内容を、新しいIDで今の階層へ貼る。
     *
     * 選んだノード（外側）は、選択範囲の左上が anchor に来るようにずらす。
     * SubTree の中身は別の階層にいるので、元の位置のまま新しい SubTree の中へ入れる。
     */
    void PasteFromJson(const nlohmann::json &clip, ImVec2 anchor);

    /// 直前の状態と比べて変わっていれば Undo に積む。グラフ変更の集約点
    void RecordStateForUndo(const std::string &commandName);

    /// 新しいIDを発行する（ノード・ピン・リンクで共通の連番）
    uint32_t GetNextId() { return ++m_idCounter; }

    /// IDからノードを引く。無ければ nullptr
    EditorBTNode *FindNode(uint32_t nodeId);

    /// エディタで選択中のエンティティが持つ BT。実行中の状態表示に使う。無ければ nullptr
    static const BehaviorTreeComponent *FindActiveAI(EditorContext &context);

    /// 行動IDの表示名（未登録なら ID 入りの代わりの文字列）
    static const char *GetActionDisplayName(ActionID id);

    /// 条件IDの表示名
    static const char *GetConditionDisplayName(ActionID id);

    /// 「ドローン：データの技」の表示名。枠番号(param)から技の名前を引く
    static const char *GetDroneFormationLabel(float slotParam);

    /// 種類ごとの見出し色。キャンバスとアウトラインで揃える
    static ImColor HeaderColorOf(EditorBTNodeType type);

    /// デコレータ種類の表示名
    static const char *DecoratorNameOf(int decoratorType);

    /// 名前を自分で付ける種類か（Selector などのまとまり・コメント枠）。
    /// Action / Condition / Decorator は中身から名前を自動で作るので false
    static bool UsesCustomName(EditorBTNodeType type);

    /// 画面に出す名前。まとまりは付けた名前、それ以外は選んだ内容＋パラメータから作る
    static std::string GetDisplayName(const EditorBTNode &node);

    /// param を「長さ 1.5秒」の形にする。label は登録表の「長さ [秒]」形式（[単位] は省略可）
    static std::string FormatParam(const char *label, float value);

    /// 実行中の評価結果（実行中でない・対応が無いなら None）
    static BTDebugState DebugStateOf(const EditorBTNode &node, const BehaviorTreeComponent *activeAI);

    /// 実行中の残り時間 [秒]。-1 はランタイム側で未開始。実行中の BT が無ければ nullopt
    static std::optional<float> DebugTimerOf(const EditorBTNode &node, const BehaviorTreeComponent *activeAI);

    // ---------------------------------------------------------------
    // BehaviorTreeWindow_Graph.cpp : 保存・読込・Bake・親子関係
    // ---------------------------------------------------------------

    /// エディタ用グラフとランタイム用配列（Bake）を1ファイルに書く
    void SaveGraph(const std::string &path);

    /// ファイルを読み、Undo の基準も引き直す。読めなければ false（今のグラフはそのまま）
    bool LoadGraph(const std::string &path);

    /// 木を字下げ付きの文章にしてファイルへ書く（AI に構造を渡して組み方を相談するため）
    void ExportOutlineText(const std::filesystem::path &path);

    /// 書き出す文章全体（凡例＋木＋未接続のノード）を作る
    std::string BuildOutlineText();

    /// 1ノード分の行と、その子の行を再帰で書く。parentId は注記（重み・主従）を決めるのに使う
    void AppendOutlineLines(std::string &out, uint32_t nodeId, uint32_t parentId, int depth,
                            std::unordered_set<uint32_t> &visited);

    /// ノード1個を JSON にする（保存項目を足すのはここと NodeFromJson だけ）
    nlohmann::json NodeToJson(const EditorBTNode &node) const;

    /// NodeToJson の逆
    static EditorBTNode NodeFromJson(const nlohmann::json &jn);

    /// グラフ全体を JSON の内容で置き換え、デバッグ表示用に空回しの Bake をする
    void ApplyGraphJson(const nlohmann::json &state);

    /// ランタイム用のフラット配列（幅優先順）を作る。runtimeFlatIndex もここで振る
    nlohmann::json BuildBakedNodes();

    /// 行動/条件の保存キー・param・weight を、意味のあるノードにだけ書く
    static void WriteKeyAndParam(nlohmann::json &j, const EditorBTNode &node);

    /// 保存キーから行動/条件IDを復元する（キーが正、数値IDは保険）
    static void RestoreActionId(EditorBTNode &node, const nlohmann::json &jn);

    /// リンクから親子関係・実行順・隠れているノードを作り直す。毎フレーム頭で呼ぶ
    //   変更経路（リンク編集・Undo・読込・ドラッグでの順序入れ替え）が多いので、汚れフラグで
    //   追うより毎回作る。数百ノードまでなら無視できるコスト。
    void RebuildHierarchy();

    /// 子を1つ以上持つか
    bool HasChildren(uint32_t nodeId) const;

    /// 子孫の数（リンクで辿れる範囲。サブツリーの中身は含まない）
    int CountDescendants(uint32_t nodeId) const;

    // ---------------------------------------------------------------
    // BehaviorTreeWindow_Layout.cpp : 自動整列・コメント枠
    // ---------------------------------------------------------------

    /// 木の形に全ノードを並べ直す。兄弟の左右順は保つので実行順は変わらない。ed::Begin の中で呼ぶ
    void AutoLayout();

    /// 選択中のノードをちょうど囲むコメント枠を作る
    void GroupSelectedNodes();

    /// コメント枠に中心が入っているノードのID
    std::vector<uint32_t> CollectNodesInGroup(uint32_t groupId) const;

    /// コメント枠の「ノード全体の左上」から「枠の左上」までのずれ
    static ImVec2 GroupFrameOffset();

    /// コメント枠の「ノード全体の寸法」−「枠の寸法」
    static ImVec2 GroupFrameExtra();

    // ---------------------------------------------------------------
    // BehaviorTreeWindow_Canvas.cpp : キャンバス
    // ---------------------------------------------------------------

    /// キャンバス全体（ノード・リンク・入力処理）
    void DrawNodeWorkspace(const BehaviorTreeComponent *activeAI);

    /// コメント枠を1つ描く。ユーザーが変えた枠の大きさもここで読み返す
    void DrawGroupNode(EditorBTNode &node);

    /// 通常ノードを1つ描く
    void DrawTreeNode(EditorBTNode &node, const BehaviorTreeComponent *activeAI);

    /// 通常ノードの本文（Priority・行動名・残り時間など）。nodeWidth はゲージの長さに使う
    void DrawTreeNodeDetail(const EditorBTNode &node, const BehaviorTreeComponent *activeAI, float nodeWidth);

    // 通常ノードの幅。名前と本文の一番長い行に合わせ、kNodeMinWidth〜kNodeMaxWidth に収める
    float TreeNodeWidthOf(const EditorBTNode &node, const std::string &title) const;

    /// 通常ノードの枠線の色と太さ（実行中・成功・失敗で色を変える）
    void GetTreeNodeBorder(const EditorBTNode &node, const BehaviorTreeComponent *activeAI,
                           ImColor &outColor, float &outWidth) const;

    /// リンクを描く。親ノードの実行状態で色を変える
    void DrawLinks(const BehaviorTreeComponent *activeAI);

    /// 自動追従：実行中のノード（Action 優先）へカメラを寄せる
    void FollowRunningNode(const BehaviorTreeComponent *activeAI);

    /// ピンのドラッグでリンクを作る。何も無い所で離したら、ノード追加メニューを予約する
    void HandleLinkCreation();

    // 線を伸ばした先に作ったノードを、伸ばし元のピンへ繋ぐ（出力から伸ばしたなら子、入力からなら親）
    void LinkNewNodeToPin(uint32_t newNodeId, uint32_t fromPinId);

    /// Delete キーでのノード・リンク削除
    void HandleDeletion();

    /// 背景の右クリック / Space で出るノード追加メニュー
    void HandleContextMenu();

    // Ctrl+V の貼り先（キャンバス座標）。カーソルがキャンバスの上ならその位置、外なら見えている範囲の中央。
    //   ed::Begin〜End の中で呼ぶ
    ImVec2 PasteTargetOnCanvas();

    /// 開いた直後に全体が収まる倍率へ合わせる
    void FitInitialView();

    // ---------------------------------------------------------------
    // BehaviorTreeWindow_Inspector.cpp : 右ペイン
    // ---------------------------------------------------------------

    /// 選択中のノードの編集欄
    void DrawInspector();

    /// 行動をカテゴリ別に選ぶプルダウン。変えたら true
    bool DrawActionCombo(const char *label, ActionID &currentId);

    /// 条件をカテゴリ別に選ぶプルダウン。変えたら true
    bool DrawConditionCombo(const char *label, ActionID &currentId);

    /// データの技（陣形）を名前で選ぶプルダウン。枠番号を param へ書く。変えたら true
    bool DrawDroneFormationCombo(const char *label, float &slotParam);

    // ---------------------------------------------------------------
    // BehaviorTreeWindow_Outliner.cpp : 左ペイン
    // ---------------------------------------------------------------

    /// 木の一覧。行を押すとキャンバスがそのノードへ移動する
    void DrawOutliner(const BehaviorTreeComponent *activeAI);

    /// 一覧の1行と、その子の行を再帰で描く。visited は同じノードを2回描かないための記録
    void DrawOutlinerRow(uint32_t nodeId, const BehaviorTreeComponent *activeAI, std::unordered_set<uint32_t> &visited);

private:
    ax::NodeEditor::EditorContext *m_nodeContext = nullptr; // ノードエディタの内部状態（視点・選択など）

    // --- グラフ本体 ---
    std::vector<EditorBTNode> m_nodes;         // 全ノード
    std::vector<BTLink>       m_links;         // 全リンク
    uint32_t                  m_idCounter = 1; // ID の連番

    // --- ファイルと Undo ---
    nlohmann::json m_lastSavedState;  // 直前に Undo へ記録した状態（変化の判定用）
    std::string    m_currentPath;     // 開いているファイル（未保存の新規なら空）
    std::string    m_pendingOpenPath; // 未保存確認中に「開く」予定のパス

    // --- サブツリーの階層 ---
    std::vector<uint32_t> m_viewPath;           // 潜っている SubTree のID（外側から順。空なら一番外）
    uint32_t              m_pendingEnterId = 0; // 次のフレームで入る SubTree（0 なら無し）

    // 貼り付けたノード。まだ一度も描かれていないノードは選択できないので、描いた後のフレームで選ぶ
    std::vector<uint32_t> m_pendingSelectIds;

    // 保存していない変更があるか。RecordStateForUndo で立て、保存・読込で下ろす。
    //   ノードを「動かしただけ」は RecordStateForUndo を通らないので検出しない。誤検出で毎回
    //   警告が出ると警告を読まなくなるので、取りこぼす側に倒してある。
    bool m_dirty = false;

    // --- キャンバス ---
    bool   m_needInitialZoom   = true;          // 開いた直後に全体へ合わせる
    int    m_framesSinceOpen   = 0;             // 全体合わせをノードのサイズ確定まで待つためのカウンタ
    ImVec2 m_newNodeSpawnPos   = {0.0f, 0.0f};  // 右クリックした画面座標（新規ノードの置き場所）

    // 線を何も無い所で離して開いたメニューの、伸ばし元のピン（0 なら普通の右クリック／Space で開いた）
    uint32_t m_newNodeLinkPin        = 0;
    bool     m_openCreateMenuForLink = false; // 次の HandleContextMenu でメニューを開く（線を離したフレームに立つ）
    ImVec2 m_canvasScreenMin   = {0.0f, 0.0f};  // キャンバスの左上（画面座標）。毎フレーム描く前に取る
    ImVec2 m_canvasScreenSize  = {0.0f, 0.0f};  // キャンバスの大きさ（画面上のピクセル）
    bool   m_layoutRequested   = false;         // 整列の予約。ed::Begin の中でしかサイズが取れないので持ち越す
    bool   m_autoFollow        = false;         // 実行中ノードへの自動追従
    uint32_t m_lastTrackedNodeId = 0;           // 最後に追従したノード

    // --- 親子関係（RebuildHierarchy が毎フレーム作り直す） ---
    std::unordered_map<uint32_t, std::vector<uint32_t>> m_childrenOf;   // 親ID → 子ID（実行順）
    std::unordered_map<uint32_t, uint32_t>              m_parentOf;     // 子ID → 親ID
    std::unordered_map<uint32_t, uint32_t>              m_pinOwner;     // ピンID → ノードID
    std::unordered_set<uint32_t>                        m_hiddenNodes;  // 描かないノード（今見ている階層に無いもの）
    std::vector<uint32_t>                               m_roots;        // 親を持たないノード（ROOT が先頭）
};
