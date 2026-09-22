// 陣形アセット（Assets/Formations/*.json）を編集するウィンドウ。
//
// 責務: 実機で動いている FormationAsset を直接書き換え、保存する。
//       形の意味は知らない（計算は Formations_DataDriven、保存は FormationAssetIO）。
//       どの項目をどう描くかは FormationAssetMeta.h の表が決め、ここはそれを回すだけ。
//
// 画面は3ペイン。「一覧 / 編集 / 見る」を同時に出す。
//   技は0.4秒ほどで終わるので、数値を触った結果をその場で止めて見られないと、
//   どの数値が何を変えたのか分からない。
//
// 編集用のコピーを持たない理由:
//   FormationRegistry が持つ実体をそのまま触れば、数値を動かしたフレームで実機の形が変わる。
//   コピーを持つと「エディタでは変わったのに画面は変わらない」が必ず起きる。
#pragma once
#include "Editor/Core/EditorWindow.h"
#include "Editor/Utils/CurveEditorWidget.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"

#include <memory>
#include <set>
#include <string>
#include <vector>

struct FieldDescriptor;
struct BehaviorTreeComponent;
struct BossCommandComponent;

namespace CCL::AI::Formation
{
    struct FormationAsset;
    struct FormationLayer;
} // namespace CCL::AI::Formation

class FormationEditorWindow : public EditorWindow
{
public:
    /// 既定は非表示。ドローンの居るシーンでしか意味が無いので Window メニューから開く。
    FormationEditorWindow();

    /// LayerSequence は .cpp にしか定義が無いので、破棄も .cpp で行う。
    ~FormationEditorWindow() override;

protected:
    void DrawContents(EditorContext &context) override;

    /// 閉じられた・畳まれたフレームでも必ず呼ばれる。実機の掌握を解くのに使う。
    /// 掌握中はボスBTを凍らせているので、ここで後始末しないとウィンドウを閉じた
    /// 瞬間にボスが永久に停止したままになる。
    void OnFrameBegin(EditorContext &context) override;

private:
    using Asset = CCL::AI::Formation::FormationAsset;
    using Layer = CCL::AI::Formation::FormationLayer;

    /// ImSequencer と layers のつなぎ。定義は .cpp（ImSequencer をヘッダへ出さないため）。
    class LayerSequence;

    // ---- 3ペイン ----
    void DrawListPane(EditorContext &context);
    void DrawEditPane(EditorContext &context);
    void DrawViewPane(EditorContext &context);

    /// 表1つぶんを「表示名 | 入力欄」の2列で描く。値が変わったら true。
    bool DrawFields(const std::vector<FieldDescriptor> &fields, void *obj);

    /// 「動き」セクション全体。再生の操作・タイムライン・選んだ1枚の詳細を縦に並べる。
    bool DrawLayers(Asset &asset);

    /// 動きの層を ImSequencer の帯として並べる。評価器の for (layer : layers) をそのまま絵にしたもの。
    ///   帯の並び = 適用順 / 帯の左右端 = windowBegin〜End / 縦線 = 予告の始まりからの経過。
    /// 帯の端や中をドラッグすると時間帯が変わる。値を変えたら true。
    bool DrawTimeline(Asset &asset);

    /// 選んでいる動きの緩急。型の一覧から選び、グラフで形を見せる。細かい形は
    /// アニメーションと同じカーブエディタで直す。値を変えたら true。
    bool DrawEase(Layer &layer);

    // ---- 実機の掌握 ----

    /// 陣形を実機へ出す。ボス停止・陣形指示・攻撃信号・時間の手送りを一度に行う。
    /// 4つ揃わないと何も見えないので、手順ではなく1つの操作にしている。
    void Stage(EditorContext &context);

    /// 掌握を解いてゲームの流れへ戻す。BTの凍結も時間の手送りも解除する。
    void Unstage(EditorContext &context);

    /// 掌握中に毎フレーム行う押し込み。BT以外がボスの命令を書いても取り返せる。
    void PushStage(EditorContext &context);

    /// 掌握で触るボスの部品。1v1 前提なので、シーンで最初に見つかったボスだけを使う。
    struct BossHandles
    {
        BehaviorTreeComponent *bt  = nullptr;
        BossCommandComponent  *cmd = nullptr;
    };
    BossHandles FindBoss(EditorContext &context) const;

    // ---- 補助 ----

    /// 今選んでいる陣形を、名前とファイルを変えて複製する。新しい陣形の作り方はこれだけで、
    /// 既存の技がそのまま雛形になる。
    bool DuplicateCurrent(const char *fileName);

    /// 今選んでいる枠の陣形。データ陣形でなければ nullptr。
    Asset *CurrentAsset() const;

    /// シーンに居るドローンの数。形の大きさを実寸で出すのに要る。
    int CountDrones(EditorContext &context) const;

    // ---- 状態 ----

    DroneFormationType m_slot = DroneFormationType::GuillotineSlam; // 開いたときに選んでいる技

    /// 保存していない変更のある技。ImGui のウィジェットが値の変更時に返す true を
    /// 溜めるだけ。元へ戻したかまでは見ない（「触った以上まだ保存していない」を
    /// 出す方が事故が少ない）。技ごとに持つのは、編集は実機の実体を直接書き換えるので、
    /// 別の技へ切り替えても変更がメモリに残り続けるため。
    std::set<DroneFormationType> m_dirtySlots;

    std::string m_status; // 直近の保存・読込の結果。押した手応えが無いと二重に押される

    // 実機の掌握
    bool  m_staged     = false;
    float m_time       = 0.0f; // 予告の始まりからの経過 [秒]。予告の長さを過ぎたら攻撃の時間
    bool  m_playing    = false;
    bool  m_slowMotion = false; // 1/4 倍で再生する（本番の速さだと攻撃は目で追えない）

    // タイムライン
    std::unique_ptr<LayerSequence> m_sequence;
    int                            m_selectedLayer = 0; // 選んでいる動き。下に詳細を出す
    int                            m_firstFrame    = 0; // 横スクロールの位置（ImSequencer が書く）

    // 緩急
    CurveEditorWidget m_curveWidget;
    const Layer      *m_curveLayer = nullptr; // カーブエディタが今見ている動き。変わったら表示範囲を合わせ直す

    char m_newFileName[64] = "NewFormation"; // 複製ダイアログの入力欄
};
