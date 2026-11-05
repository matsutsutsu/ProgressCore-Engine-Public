#pragma once
// ===========================================================================
// 概要: カーブデータ（複数の CurveData を1ファイルにまとめたもの）を編集する単独のウィンドウ
//
// 責務:
//   - 複数のカーブ（RGB / XYZ / 各種スカラーの演出）の追加・削除・表示切り替え
//   - キャンバスでのキーと接線の編集（補間: 定数 / 線形 / 3次エルミート、
//     接線: 自動 / フラット / 揃える / 分離、外挿: 定数 / 線形 / 繰り返し / 往復 / オフセット繰り返し）
//   - JSON の読み書き
//
// 備考: キャンバスの操作は CurveEditorWidget と同じだが、実装は共有していない（このウィンドウが
//       自前で持つ）
// ===========================================================================
#include "Editor/Core/EditorWindow.h"
#include "Engine/Core/Math/CurveData.h"

#include <string>
#include <vector>
#include <set>
#include <unordered_map>

#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif

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
    std::string            m_docName = "新しいカーブセット";
    std::vector<CurveData> m_curves;
    int                    m_activeCurve = 0;
    std::string            m_currentFilePath;

    std::set<uint32_t> m_selectedKeys; // アクティブカーブ内の選択キーID

    // 表示範囲（time, value）。ズーム・パンで更新される
    ImVec2 viewMin = ImVec2(-0.1f, -0.2f);
    ImVec2 viewMax = ImVec2(1.1f, 1.2f);

    // プレビューの再生ヘッド
    float m_previewTime = 0.0f;
    bool  m_showPreview = true;

    // -----------------------------------------------------------------------
    // 操作の一時状態
    // -----------------------------------------------------------------------
    DragMode                             m_drag = DragMode::None;
    ImVec2                               m_dragStartCurve;   // ドラッグ開始のカーブ座標
    std::unordered_map<uint32_t, ImVec2> m_dragOrig;         // 移動前の (time,value)
    uint32_t                             m_tangentKeyId = 0; // 接線ドラッグ対象
    ImVec2                               m_boxStartScreen;   // 矩形選択の始点(スクリーン)

    // コンテキストメニュー用
    uint32_t m_ctxKeyId    = 0;
    ImVec2   m_ctxCurvePos = ImVec2(0, 0);

    uint32_t m_paletteIndex = 0; // 新しいカーブに次に割り当てる色の番号
};