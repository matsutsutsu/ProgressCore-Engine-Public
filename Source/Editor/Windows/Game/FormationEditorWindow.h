#pragma once
// ===========================================================================
// 概要: 陣形アセット（Assets/Formations/*.json）を編集するウィンドウ
//
// 責務:
//   - 実機で動いている FormationAsset を直接書き換え、保存・読み直し・複製する
//   - 動きの層をタイムラインで編集し、緩急をカーブで直す
//   - ボスAIを止めて陣形を実機に出し、再生・手送りで確かめる
//
// 備考: 形の意味は扱わない（計算は DataDrivenFormation、保存は FormationAssetIO、項目の表は FormationAssetMeta.h）
//       技は0.4秒ほどで終わるので、「一覧 / 編集 / 見る」の3ペインを同時に出し、触った結果をその場で止めて見る
//       編集用のコピーを持たず、FormationRegistry の実体を直接触る。コピーだと、エディタで変えても画面が変わらない
// ===========================================================================
#include "Editor/Core/EditorWindow.h"
#include "Editor/Utils/CurveEditorWidget.h"
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"

#include <memory>
#include <set>
#include <string>
#include <vector>

struct FieldDescriptor;
struct BehaviorTreeComponent;
struct BossCommandComponent;
class FormationLayerSequence;

namespace CCL::AI::Formation
{
    struct FormationAsset;
    struct FormationLayer;
    class FormationRegistry;
}

namespace CCL::AI::Formation::Data { class DataDrivenFormation; }

class FormationEditorWindow : public EditorWindow
{
public:
    // 既定は非表示。ドローンのいるシーンでしか意味がないので Window メニューから開く
    FormationEditorWindow();

    // FormationLayerSequence の定義はこのヘッダにないので、破棄も .cpp で行う
    ~FormationEditorWindow() override;

protected:
    void DrawContents(EditorContext &context) override;

    // 閉じた・畳んだフレームでも必ず呼ばれる。実機の掌握を解くのに使う
    // 掌握中はボスの BT を止めているので、ここで後始末しないと、閉じた後もボスが止まったままになる
    void OnFrameBegin(EditorContext &context) override;

private:
    using Asset = CCL::AI::Formation::FormationAsset;
    using Layer = CCL::AI::Formation::FormationLayer;

    // ---- 3ペイン ----
    void DrawListPane(EditorContext &context);
    void DrawEditPane(EditorContext &context);
    void DrawViewPane(EditorContext &context);

    // 表1つぶんを「表示名 | 入力欄」の2列で描く。値が変わったら true
    bool DrawFields(const std::vector<FieldDescriptor> &fields, void *obj);

    // 「動き」セクション全体。下の4つを縦に並べるだけ。値が変わったら true
    bool DrawLayers(Asset &asset);

    // 再生・停止・スロー。タイムラインの真上に置く（再生位置と帯が同じ時間軸だと目で分かるように）
    void DrawPlayback(const Asset &asset);

    // 動きの追加・並べ替え・削除。操作の対象は「選んでいる動き」。値が変わったら true
    bool DrawLayerButtons(Asset &asset);

    // 選んでいる動き1枚の詳細。項目の表と、置いても効かない組み合わせの警告。値が変わったら true
    bool DrawLayerDetail(Layer &layer);

    // 動きの層を ImSequencer の帯として並べる。帯の並びが適用順、左右の端が windowBegin〜End
    // 縦線は予告の始まりからの経過。帯をドラッグすると時間帯が変わる。値を変えたら true
    bool DrawTimeline(Asset &asset);

    // 選んでいる動きの緩急を、型の一覧とグラフで編集する。細かい形はカーブエディタで直す。値を変えたら true
    bool DrawEase(Layer &layer);

    // 緩急の形のグラフ。評価器と同じ Ease を引くので、ここで見える形がそのまま実機の動きになる
    void DrawEaseGraph(const Layer &layer);

    // 緩急の型のキーから、動きの層へ入れるカーブを作る。id は「どの型から作ったか」の覚え書き
    static CurveData MakeEaseCurve(const char *id, const CurveKeyframe *keys, int count);

    // ---- 実機の掌握 ----

    // 陣形を実機へ出す。ボスの停止・陣形の指示・攻撃信号・時間の手送りは、4つそろわないと何も見えないので1つの操作にする
    void Stage(EditorContext &context);

    // 掌握を解いてゲームの流れへ戻す。BTの停止も時間の手送りも解除する
    void Unstage(EditorContext &context);

    // 掌握中に毎フレーム行う押し込み。BT以外がボスの命令を書いても取り返せる
    void PushStage(EditorContext &context);

    // 掌握で触るボスの部品。1v1 前提なので、シーンで最初に見つかったボスだけを使う
    struct BossHandles
    {
        BehaviorTreeComponent *bt  = nullptr;
        BossCommandComponent  *cmd = nullptr;
    };
    BossHandles FindBoss(EditorContext &context) const;

    // ---- 補助 ----

    // 今選んでいる陣形を、名前とファイルを変えて複製する（Assets/Formations/ に作る）。新しい陣形はこれで作る
    // 同名のファイルがある・空き枠がない・保存に失敗したときは false（理由は m_status）
    bool DuplicateCurrent(const char *fileName);

    // その枠のデータ陣形。C++ 実装の枠・空き枠・シーンがないときは nullptr
    CCL::AI::Formation::Data::DataDrivenFormation *DataAt(DroneFormationType slot) const;

    // 今選んでいる枠の陣形。データ陣形でなければ nullptr
    Asset *CurrentAsset() const;

    // シーンにいるドローンの数。形の大きさを実寸で出すのに要る
    int CountDrones(EditorContext &context) const;

    // ---- 状態 ----

    // 今の World の陣形レジストリ。OnFrameBegin が毎フレーム引き直す。シーンがなければ nullptr
    CCL::AI::Formation::FormationRegistry *m_registry = nullptr;

    DroneFormationType m_slot = DroneFormationType::GuillotineSlam; // 開いたときに選んでいる技

    // 保存していない変更のある技。ウィジェットが返す「変えた」を溜めるだけで、元へ戻したかは見ない
    // 編集は実機の実体を直接書き換えるので、ほかの技へ切り替えても変更が残る。そのため技ごとに持つ
    std::set<DroneFormationType> m_dirtySlots;

    std::string m_status; // 直近の保存・読込の結果。押した手応えがないと二重に押される

    // 実機の掌握
    bool  m_staged     = false;
    float m_time       = 0.0f; // 予告の始まりからの経過 [秒]。予告の長さを過ぎたら攻撃の時間
    bool  m_playing    = false;
    bool  m_slowMotion = false; // 1/4 倍で再生する（本番の速さだと攻撃は目で追えない）

    // タイムライン
    std::unique_ptr<FormationLayerSequence> m_sequence;      // 動きの層と ImSequencer のつなぎ
    int                                    m_selectedLayer = 0; // 選んでいる動き。下に詳細を出す
    int                                    m_firstFrame    = 0; // 横スクロールの位置（ImSequencer が書く）

    // 緩急
    CurveEditorWidget m_curveWidget;
    const Layer      *m_curveLayer = nullptr; // カーブエディタが今見ている動き。変わったら表示範囲を合わせ直す

    char m_newFileName[64] = "NewFormation"; // 複製ダイアログの入力欄
};
