#pragma once
#include "Editor/Core/EditorWindow.h"
#include "Engine/Core/Math/CurveData.h"

#include <string>
#include <vector>
#include <set>
#include <unordered_map>

#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif

// ===========================================================================
// 汎用カーブエディタ
//   - 複数カーブ（RGB / XYZ / 各種スカラー演出）を1ドキュメントで編集
//   - 補間: Constant / Linear / Cubic(エルミート)
//   - 接線: Auto / Flat / Aligned / Broken（ハンドルドラッグ対応）
//   - 外挿: Constant / Linear / Cycle / PingPong / CycleOffset
// ===========================================================================
class CurveEditorWindow : public EditorWindow
{
  public:
    CurveEditorWindow()
        : EditorWindow("Curve Editor")
    {
    }
    virtual ~CurveEditorWindow() = default;

  protected:
    void DrawContents(EditorContext &context) override;

  private:
    // ドラッグ操作の状態
    enum class DragMode
    {
        None,
        MoveKeys,   // 選択キーの移動
        TangentIn,  // in 接線ハンドル
        TangentOut, // out 接線ハンドル
        BoxSelect   // 矩形選択
    };

    // --- UI パーツ ---
    void EnsureInitialized();
    void DrawToolbar();
    void DrawCurveList();
    void DrawCanvas(float height);
    void DrawInspector();

    // --- 操作 ---
    void AddCurve();
    void FrameAll(); // 全キーが収まるように表示範囲を合わせる
    void DeleteSelectedKeys();
    void SaveToFile(const std::string &path);
    bool LoadFromFile(const std::string &path);

    CurveData       *ActiveCurve();
    const CurveData *ActiveCurve() const;

    // -----------------------------------------------------------------------
    // データ
    // -----------------------------------------------------------------------
    std::string            _docName = "新しいカーブセット";
    std::vector<CurveData> _curves;
    int                    _activeCurve = 0;
    std::string            _currentFilePath;

    std::set<uint32_t> _selectedKeys; // アクティブカーブ内の選択キーID

    // 表示範囲（time, value）。ズーム/パンで更新される
    ImVec2 _viewMin = ImVec2(-0.1f, -0.2f);
    ImVec2 _viewMax = ImVec2(1.1f, 1.2f);

    // プレビュー再生ヘッド
    float _previewTime = 0.0f;
    bool  _showPreview = true;

    // 補助トグル
    bool _snap = false; // Ctrl中はグリッドスナップ（このフラグは常時ONにもできる）

    // -----------------------------------------------------------------------
    // 操作の一時状態
    // -----------------------------------------------------------------------
    DragMode                             _drag = DragMode::None;
    ImVec2                               _dragStartCurve;   // ドラッグ開始のカーブ座標
    std::unordered_map<uint32_t, ImVec2> _dragOrig;         // 移動前の (time,value)
    uint32_t                             _tangentKeyId = 0; // 接線ドラッグ対象
    ImVec2                               _boxStartScreen;   // 矩形選択の始点(スクリーン)

    // コンテキストメニュー用
    uint32_t _ctxKeyId    = 0;
    ImVec2   _ctxCurvePos = ImVec2(0, 0);

    uint32_t _paletteIndex = 0; // 新規カーブの色巡回
};