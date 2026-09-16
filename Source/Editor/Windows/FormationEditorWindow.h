// 陣形アセット（Assets/Formations/*.json）を編集するウィンドウ。
//
// 責務: 実機で動いている FormationAsset を直接書き換え、保存する。
//       形の意味は知らない（計算は Formations_DataDriven、保存は FormationAssetIO）。
//
// 画面は3ペイン。「一覧 / 編集 / 見る」を同時に出す。
//   触った結果が見えないと、どの数値が何を変えたのか分からない。編集と確認を
//   別ウィンドウに分けた最初の版は、確認のたびに Drone Manager へ切り替えて
//   技を出し直す必要があり、しかも技は0.4秒で終わるので実質使えなかった。
//
// コピーを持たない理由:
//   編集用のコピーを持つと「エディタでは変わったのに画面は変わらない」が必ず起きる。
//   FormationRegistry が持っている実体をそのまま触れば、スライダーを動かした
//   フレームで100機の座標が変わる。
//
// 模式図を描かない理由:
//   2026-08 の実装はウィンドウ内に上から/横からの点群を描いていたが、実機では
//   同じ形が100機の3Dで動いている。模式図はその劣化コピーで、座標変換のコードを
//   抱えるわりに情報が少ない。実機を止めて進行度を手で送る方が確実。
//
// 内部用語を画面に出さない:
//   「席」「窓」「ずらす基準」は設計を説明するための言葉で、使う側の言葉ではない。
//   画面には動作で書く（「前へ倒す」「左から順に」）。この対応表は .cpp の
//   kMotionNames / kOrderNames にある。
#pragma once
#include "Editor/Core/EditorWindow.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"

#include <string>

namespace CCL::AI::Formation
{
    struct FormationAsset;
    struct FormationLayer;
} // namespace CCL::AI::Formation

class FormationEditorWindow : public EditorWindow
{
public:
    /// 既定は非表示。ドローンの居るシーンでしか意味が無いので Window メニューから開く。
    FormationEditorWindow()
        : EditorWindow("Formation Editor", false)
    {
    }

protected:
    void DrawContents(EditorContext &context) override;

    /// 閉じられた・畳まれたフレームでも必ず呼ばれる。実機の掌握を解くのに使う。
    /// 掌握中はボスBTを凍らせているので、ここで後始末しないとウィンドウを閉じた
    /// 瞬間にボスが永久に停止したままになる。
    void OnFrameBegin(EditorContext &context) override;

private:
    using Asset = CCL::AI::Formation::FormationAsset;
    using Layer = CCL::AI::Formation::FormationLayer;

    // ---- 3ペイン ----
    void DrawListPane(EditorContext &context);
    void DrawEditPane(EditorContext &context);
    void DrawViewPane(EditorContext &context);

    // ---- 編集ペインの中身 ----
    void DrawShapeSection(Asset &asset, EditorContext &context);
    void DrawPlacementSection(Asset &asset);
    void DrawMotionSection(Asset &asset);
    void DrawLayerBody(Asset &asset, Layer &layer);
    void DrawSpeedSection(Asset &asset);
    void DrawCombatSection(Asset &asset);

    // ---- 実機の掌握 ----

    /// 陣形を実機へ出す。ボス停止・陣形指示・攻撃信号・進行度の手送りを一度に行う。
    /// 4つ揃わないと何も見えないので、手順ではなく1つの操作にしている。
    void Stage(EditorContext &context);

    /// 掌握を解いてゲームの流れへ戻す。BTの凍結も進行度の手送りも解除する。
    void Unstage(EditorContext &context);

    /// 掌握中に毎フレーム行う押し込み。BT以外がボスの命令を書いても取り返せる。
    void PushStage(EditorContext &context);

    // ---- 新規作成 ----

    /// 雛形から陣形を1つ作り、空き枠へ登録して選択状態にする。
    /// 白紙から組ませないのは、「何から始めればいいか分からない」が
    /// このエディタで最初にぶつかる壁だったため。
    bool CreateFromTemplate(int templateIndex, const char *fileName);

    /// 今選んでいる陣形を、名前とファイルを変えて複製する。
    bool DuplicateCurrent(const char *fileName);

    // ---- 補助 ----

    /// 直前に描いたウィジェットにマウスが乗っていたら、右ペインへ出す説明を差し替える。
    /// ツールチップにしないのは、読みながら操作できないため（離すと消える）。
    void Help(const char *title, const char *body);

    /// 今選んでいる枠の陣形。データ陣形でなければ nullptr。
    Asset *CurrentAsset() const;

    /// シーンに居るドローンの数。形の大きさを実寸で出すのに要る。
    int CountDrones(EditorContext &context) const;

    // ---- 状態 ----

    DroneFormationType m_slot = DroneFormationType::DataSlot00;

    /// 保存していない変更があるか。ImGui のウィジェットが値の変更時に返す true を
    /// 溜めるだけ。元へ戻したかまでは見ない（「触った以上まだ保存していない」を
    /// 出す方が事故が少ない）。
    bool m_dirty = false;

    std::string m_status; // 直近の保存・読込の結果。押した手応えが無いと二重に押される

    // 実機の掌握
    bool  m_staged   = false;
    float m_progress = 0.0f;
    bool  m_playing  = false;

    // 右ペインの説明欄。ホバーが外れても消さない（読んでいる途中で消えるため）
    std::string m_helpTitle;
    std::string m_helpBody;

    // 新規作成・複製のダイアログ
    int  m_newTemplate     = 0;
    char m_newFileName[64] = "NewFormation";
};
