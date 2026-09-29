#pragma once
// ===========================================================================
// 概要: 他のウィンドウに埋め込んで使うカーブ編集ウィジェット。
//
// 責務:
//   - 複数の CurveData を1枚のキャンバスに描き、アクティブな1本のキーを編集する
//   - パン・ズーム・グリッド・再生ヘッドの表示
//   - キーの追加・移動・範囲選択・削除、接線の編集、補間と外挿の設定
//
// 備考: ウィンドウ枠やファイルの読み書きは持たない。CurveData はポインタで受け取り、
//       所有しない。CurveEditorWindow とシーケンサーの Curve ペインがこれを共有し、
//       同じ操作感と同じ評価処理（CurveData）を使う。
//
// @code
//   std::vector<CurveData*> targets = { &seq.speedCurve, &seq.rootMotionCurve };
//   CurveEditorWidget::Options opt;
//   opt.height = 260.0f; opt.showPlayhead = true; opt.playheadX = previewTime;
//   bool changed = m_curveWidget.Draw("##seqCurves", targets, opt);
//   if (changed) { /* プレビュー更新・変更済みの印など */ }
// @endcode
// ===========================================================================
#include <vector>
#include <set>
#include <cstdint>
#include "Engine/Core/Math/CurveData.h"

class CurveEditorWidget
{
public:
    struct Options
    {
        float height        = 260.0f; // キャンバスの高さ [px]
        bool  showInspector = true;   // キャンバスの下にキー・カーブの設定欄を出す
        bool  showPlayhead  = false;  // 再生ヘッドを描く
        float playheadX     = 0.0f;   // 再生ヘッドの位置（カーブの横軸の値。時間または進捗）
        bool  fitRequested  = false;  // true のフレームだけ、全カーブが収まるよう表示範囲を合わせる
    };

    // このフレームでいずれかのカーブが変更されたら true を返す
    bool Draw(const char *strId, const std::vector<CurveData *> &curves, Options &opts);

    void FrameAll(const std::vector<CurveData *> &curves); // 全カーブが収まるよう表示範囲を合わせる
    void SetActiveCurve(int idx);                          // 編集するカーブを切り替える（キーの選択は解除する）
    int  ActiveCurveIndex() const
    {
        return m_activeCurve;
    }

private:
    // 表示範囲（カーブ座標）
    float m_viewMinX = 0.0f, m_viewMaxX = 1.0f;
    float m_viewMinY = -0.2f, m_viewMaxY = 1.2f;

    int                m_activeCurve = 0; // 編集中のカーブ（curves 内の添字）
    std::set<uint32_t> m_sel;             // 選択中のキーの ID（編集中のカーブの中でだけ有効）

    enum class Drag
    {
        None,
        MoveKeys,
        TangentIn,
        TangentOut,
        BoxSelect
    };
    Drag     m_drag       = Drag::None;
    uint32_t m_tangentKey = 0; // 接線をドラッグしているキーの ID

    // MoveKeys 用の開始時の値（キー ID → 元の time, value）
    std::vector<std::pair<uint32_t, std::pair<float, float>>> m_dragOrig;
    float                                                     m_dragStartX = 0.0f, m_dragStartY = 0.0f;
    float                                                     m_boxStartX = 0.0f, m_boxStartY = 0.0f;

    // 右クリックメニューを開いた位置（カーブ座標）と、その下にあったキーの ID（0 なら無し）
    float    m_ctxX = 0.0f, m_ctxY = 0.0f;
    uint32_t m_ctxKey = 0;

    bool DrawCanvas(const char *strId, const std::vector<CurveData *> &curves, Options &opts);
    void DrawInspector(const std::vector<CurveData *> &curves, bool &modified);
    void DeleteSelected(CurveData *curve, bool &modified);
};