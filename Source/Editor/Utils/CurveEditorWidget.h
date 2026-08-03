#pragma once
#include <vector>
#include <set>
#include <cstdint>
#include "Engine/Core/Math/CurveData.h"

// ===========================================================================
// CurveEditorWidget
//   ウィンドウchrome・ファイルIOを持たない「埋め込み用」カーブ編集ウィジェット。
//   複数の CurveData* を1枚のキャンバス上で表示・編集する（ポインタは所有しない）。
//   スタンドアロンの CurveEditorWindow も、シーケンサーの Curve ペインも、
//   これを共有して同一の操作感・同一の評価ロジック(CurveData)を使う（SSOT）。
//
//   使い方:
//     std::vector<CurveData*> targets = { &seq.speedCurve, &seq.rootMotionCurve };
//     CurveEditorWidget::Options opt;
//     opt.height = 260.0f; opt.showPlayhead = true; opt.playheadX = previewTime;
//     bool changed = m_curveWidget.Draw("##seqCurves", targets, opt);
//     if (changed) { /* プレビュー更新・ダーティ化など */ }
// ===========================================================================
class CurveEditorWidget
{
  public:
    struct Options
    {
        float height        = 260.0f; // キャンバス高さ(px)
        bool  showInspector = true;   // 右側にキー/カーブ設定パネルを出す
        bool  showPlayhead  = false;  // 再生ヘッドを描く
        float playheadX     = 0.0f;   // 再生ヘッドのX(カーブ座標＝時間 or 進捗)
        bool  fitRequested  = false;  // このフレームだけ全体表示にフィット
    };

    // 戻り値: このフレームでいずれかのカーブが変更されたら true
    bool Draw(const char *strId, const std::vector<CurveData *> &curves, Options &opts);

    void FrameAll(const std::vector<CurveData *> &curves);
    void SetActiveCurve(int idx);
    int  ActiveCurveIndex() const
    {
        return m_activeCurve;
    }

  private:
    // ビュー範囲（カーブ座標）
    float m_viewMinX = 0.0f, m_viewMaxX = 1.0f;
    float m_viewMinY = -0.2f, m_viewMaxY = 1.2f;

    int                m_activeCurve = 0; // 編集アクティブなカーブ（curves内index）
    std::set<uint32_t> m_sel;             // 選択キーID（アクティブカーブ内で有効）

    enum class Drag
    {
        None,
        MoveKeys,
        TangentIn,
        TangentOut,
        BoxSelect
    };
    Drag     m_drag       = Drag::None;
    uint32_t m_tangentKey = 0;

    // MoveKeys 用スナップショット（キーID→元の time,value）
    std::vector<std::pair<uint32_t, std::pair<float, float>>> m_dragOrig;
    float                                                     m_dragStartX = 0.0f, m_dragStartY = 0.0f;
    float                                                     m_boxStartX = 0.0f, m_boxStartY = 0.0f;

    // コンテキストメニュー用
    float    m_ctxX = 0.0f, m_ctxY = 0.0f;
    uint32_t m_ctxKey = 0;

    bool DrawCanvas(const char *strId, const std::vector<CurveData *> &curves, Options &opts);
    void DrawInspector(const std::vector<CurveData *> &curves, bool &modified);
    void DeleteSelected(CurveData *curve, bool &modified);
};