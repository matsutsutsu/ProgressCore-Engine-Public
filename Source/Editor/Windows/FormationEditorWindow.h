// 陣形(FormationSpec)をパラメータで組み立てるエディタウィンドウ。
//        役割     : Assets/Formations/*.json を見ながら編集・保存する。
//        責務     : 割付/形/変換/時間/見せ/出力 の6ブロックと、その結果の可視化。
//        位置付け : 既存 EditorWindow 群の一つ。Window メニューからトグルする。
//        設計方針 : registry が持つ spec を直接触る。コピーを持たないので、
//                   触った瞬間に実機の全ドローンへ反映される。
//
// プレビューを実機と別に持つ理由:
//   実機プレビューはボスとプレイヤーが居ないと動かない。形だけを詰めたい段階で
//   シーンを立ち上げるのは重いので、ボス原点・プレイヤー正面という決め打ちの
//   場でも同じ IFormation を評価して見せる。評価経路は実機とまったく同じ。
#pragma once
#include "Editor/Core/EditorWindow.h"
#include "Editor/Utils/CurveEditorWidget.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"
#include "Game/Logics/AI/Drone/Formation/Formations_DataDriven.h"

#include <string>
#include <vector>

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

    // ---- パラメータ編集（左ペイン）----
    void DrawAssignSection(Spec &spec);
    void DrawShapeSection(Spec &spec);
    void DrawTransformSection(Spec &spec);
    void DrawTimingSection(Spec &spec);
    void DrawTellSection(Spec &spec);
    void DrawOutputSection(Spec &spec);

    // ラベルの右に (?) を出し、ホバーで説明を見せる。
    // 用語（符号グループ・進行度など）を知らなくても触れるようにするため。
    void Hint(const char *text);

    // 割付の設定が実際に何機をどう並べるのかを1行で出す。
    // 数字を入れても結果が想像できないのがこのブロックの最大のつまずきどころ。
    void DrawAssignSummary(const Spec &spec);

    // ---- プレビュー（右ペイン）----
    void DrawPreviewPane(Data &formation);

    // 指定の進行度で全機の座標と席を計算する。
    void EvaluatePoints(Data &formation, float progress);

    /**
     * @brief 1枚のキャンバスを描く。
     * @param axisH 横軸に使う成分（0=X/右, 2=Z/前）
     * @param axisV 縦軸に使う成分（1=Y/上, 2=Z/前）
     */
    void DrawCanvas(const char *label, const char *caption, int axisH, int axisV, float height);

    // 点の色。グループが複数あるならグループ別、1つなら並び順のグラデーション。
    unsigned int SeatColor(size_t index, const Data::Seat &seat) const;

    void DrawLegend();

    // ---- 実機連携・保存 ----
    void DrawBossControls(EditorContext &context, DroneFormationType type);
    void DrawAssetControls(EditorContext &context, Data &formation, const std::string &path);

    // 保存済みの内容を控える。以後これとの差で「変更あり」を判定する。
    void CaptureBaseline(const Spec &spec);

    // C++ 実装とデータ実装を同じ条件で走らせ、座標のずれを測る。
    // データ化が正しいかは「見た目が変わらないこと」でしか確かめられないので、
    // 目視の前にまず数値で出しておく。
    void RunAbCheck(EditorContext &context, DroneFormationType type);

    // ---- 状態 ----
    int   m_editTarget      = 0;     // 編集対象（DroneFormationSystem::SpecAssets のインデックス）
    int   m_previewDrones   = 100;   // プレビューの機数
    float m_previewProgress = 0.0f;  // スクラブ位置
    bool  m_playing         = false; // 自動再生

    // 保存済みの内容。これと今の spec を比べて未保存かどうかを出す。
    std::string m_baseline;
    int         m_baselineTarget = -1;

    // 直近の照合結果。未実行なら m_abMaxError < 0。
    bool  m_abDone          = false;
    float m_abMaxError      = -1.0f;
    int   m_abWorstIndex    = -1;
    float m_abWorstProgress = 0.0f;

    // 計算結果の置き場。毎フレーム作り直すが、確保は使い回す。
    std::vector<DirectX::SimpleMath::Vector3> m_points;
    std::vector<Data::Seat>                   m_seats;

    // 表示範囲。スクラブで揺れないよう、進行度を通した全体の広がりで決める。
    DirectX::SimpleMath::Vector3 m_boundsMin;
    DirectX::SimpleMath::Vector3 m_boundsMax;

    // 凡例と色分けに使う、今の spec のグループ数。
    int m_sideGroups = 1;
    int m_tierGroups = 1;

    CurveEditorWidget m_curveWidget;
};
