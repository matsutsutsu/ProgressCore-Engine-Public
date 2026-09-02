// 陣形(FormationSpec)を編集するエディタウィンドウ。
//        役割     : Assets/Formations/*.json を編集し、実機で確かめて保存する。
//        責務     : 型の選択・主要パラメータ・進行度の手送り・保存。
//        位置付け : Window メニューからトグルする EditorWindow の一つ。
//        設計方針 : registry が持つ spec を直接触る。コピーを持たないので、
//                   触った瞬間に実機の全ドローンへ反映される。
//
// 確認を実機だけで行う理由:
//   以前はこのウィンドウ内に2Dの模式図（上から/横から の点群）を描いていた。
//   だが実機では同じ形が100機の3Dで動いている。模式図はその劣化コピーで、
//   座標変換のコードを抱えるわりに情報が少ない。
//   ボスを止めて進行度を手で送れるようにすれば「本物を止めて見る」ができるので、
//   模式図そのものが要らなくなる。
#pragma once
#include "Editor/Core/EditorWindow.h"
#include "Editor/Utils/CurveEditorWidget.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"
#include "Game/Logics/AI/Drone/Formation/Formations_DataDriven.h"

#include <string>

struct BehaviorTreeComponent;
struct BossCommandComponent;

class FormationEditorWindow : public EditorWindow
{
  public:
    // 既定は非表示（必要時に Window メニューから開く）。
    FormationEditorWindow()
        : EditorWindow("Formation Editor", false)
    {
    }

  protected:
    void DrawContents(EditorContext &context) override;

  private:
    using Spec = CCL::AI::Formation::FormationSpec;
    using Data = CCL::AI::Formation::DataDrivenFormation;

    // ---- 実機に出す ----
    //
    // 陣形を見るには「ボスAIを止める・その陣形を要求する・攻撃状態にする・
    // 進行度を手送りに切り替える」の4つが同時に要る。
    // 手順として並べたところで、4つ全部そろうまで何も見えないことは変わらない。
    // なので4つまとめて1つのボタンにする。
    void DrawStageControls(EditorContext &context, DroneFormationType type);

    // 動きの「型」。
    //
    // なぜ型を先に選ばせるか:
    //   spec は「割付・形・変換・時間」に分解されていて組み合わせとしては強いが、
    //   白紙からその組み合わせを組み上げて目的の動きへ辿り着くのは無理がある。
    //   出荷済みの7陣形を見ると、意味のある組み合わせは実質6種類しかない。
    //   ならばその6種類をボタンにして、あとは大きさと速さだけ触る形にする。
    void DrawPresetGallery(Spec &spec);

    // 型を選んだあとに詰める値。
    //   出す項目は spec のフィールドではなく「結果」で選ぶ。
    //   例: timing.speed [1/秒] ではなく「振り切るまでの時間 [秒]」として出す。
    void DrawParameters(Spec &spec);

    // 今の設定が何を作るのかを日本語1〜2行で書き出す。
    // 数字と実際の動きが結び付かないのがこのUIの最大のつまずきなので、
    // 設定を文章として読み返せるようにする。
    void DrawSpecSummary(const Spec &spec);

    // 保存・破棄・再読込・照合。
    void DrawAssetControls(EditorContext &context, Data &formation, const std::string &path);

    // C++ 実装とデータ実装を同じ条件で走らせ、座標のずれを測る。
    // データ化が正しいかは「見た目が変わらないこと」でしか確かめられないので、
    // 目視の前にまず数値で出しておく。
    void RunAbCheck(EditorContext &context, DroneFormationType type);

    // ---- 補助 ----

    // ラベルの右に (?) を出し、ホバーで説明を見せる。
    void Hint(const char *text);

    // シーン内の最初のボスを引く。居なければ false（ボス無しシーンでも開けるように）。
    bool FindBoss(EditorContext &context, BehaviorTreeComponent **bt, BossCommandComponent **cmd);

    // 実機の掌握を解いてゲームの流れへ戻す。進行度の手送りも解除する。
    void ReleaseStage(EditorContext &context);

    // 保存済みの内容を控える。以後これとの差で「変更あり」を判定する。
    void CaptureBaseline(const Spec &spec);

    // ---- 状態 ----
    int   m_editTarget = 0;     // 編集対象（DroneFormationSystem::SpecAssets のインデックス）
    float m_progress   = 0.0f;  // 手送りしている進行度
    bool  m_playing    = false; // 自動再生
    bool  m_staged     = false; // 実機を掌握しているか（ボス停止＋進行度の手送り）

    // 保存済みの内容。これと今の spec を比べて未保存かどうかを出す。
    std::string m_baseline;
    int         m_baselineTarget = -1;

    // 直近の照合結果。未実行なら m_abMaxError < 0。
    bool  m_abDone          = false;
    float m_abMaxError      = -1.0f;
    int   m_abWorstIndex    = -1;
    float m_abWorstProgress = 0.0f;

    CurveEditorWidget m_curveWidget;
};
