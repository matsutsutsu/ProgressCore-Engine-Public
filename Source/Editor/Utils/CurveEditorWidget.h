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
//     bool changed = _curveWidget.Draw("##seqCurves", targets, opt);
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
        return _activeCurve;
    }

  private:
    // ビュー範囲（カーブ座標）
    float _viewMinX = 0.0f, _viewMaxX = 1.0f;
    float _viewMinY = -0.2f, _viewMaxY = 1.2f;

    int                _activeCurve = 0; // 編集アクティブなカーブ（curves内index）
    std::set<uint32_t> _sel;             // 選択キーID（アクティブカーブ内で有効）

    enum class Drag
    {
        None,
        MoveKeys,
        TangentIn,
        TangentOut,
        BoxSelect
    };
    Drag     _drag       = Drag::None;
    uint32_t _tangentKey = 0;

    // MoveKeys 用スナップショット（キーID→元の time,value）
    std::vector<std::pair<uint32_t, std::pair<float, float>>> _dragOrig;
    float                                                     _dragStartX = 0.0f, _dragStartY = 0.0f;
    float                                                     _boxStartX = 0.0f, _boxStartY = 0.0f;

    // コンテキストメニュー用
    float    _ctxX = 0.0f, _ctxY = 0.0f;
    uint32_t _ctxKey = 0;

    bool DrawCanvas(const char *strId, const std::vector<CurveData *> &curves, Options &opts);
    void DrawInspector(const std::vector<CurveData *> &curves, bool &modified);
    void DeleteSelected(CurveData *curve, bool &modified);
};