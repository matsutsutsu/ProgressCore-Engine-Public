#include "CurveEditorWindow.h"

#include <imgui.h>
#include <imgui_internal.h>

#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Dialog.h"
#include "Engine/Platform/Core/Logger.h"
#include <json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

// ---------------------------------------------------------------------------
// ローカルヘルパ
// ---------------------------------------------------------------------------
namespace
{
    // 「きりの良い」目盛り間隔を選ぶ（1,2,5 * 10^n）
    float NiceStep(float range, float targetLines)
    {
        if (range <= 0.0f || targetLines <= 0.0f) return 1.0f;
        const float raw  = range / targetLines;
        const float mag  = std::pow(10.0f, std::floor(std::log10(raw)));
        const float norm = raw / mag; // 1..10
        float       nice;
        if (norm < 1.5f)
            nice = 1.0f;
        else if (norm < 3.0f)
            nice = 2.0f;
        else if (norm < 7.0f)
            nice = 5.0f;
        else
            nice = 10.0f;
        return nice * mag;
    }

    /**
     * @brief 目盛りの数値を文字列にする。小数の桁数は目盛りの間隔に合わせて変える。
     * @param buf  書き込み先のバッファ。
     * @param n    buf の大きさ（バイト）。
     * @param v    書く値。
     * @param step 目盛りの間隔。1 未満なら小数1桁、0.1 未満なら2桁、0.01 未満なら3桁。
     */
    void FormatTick(char *buf, size_t n, float v, float step)
    {
        int decimals = 0;
        if (step < 1.0f) decimals = 1;
        if (step < 0.1f) decimals = 2;
        if (step < 0.01f) decimals = 3;
        std::snprintf(buf, n, "%.*f", decimals, v);
    }

    // 破線
    void AddDashedLine(ImDrawList *dl, ImVec2 a, ImVec2 b, ImU32 col, float thickness, float dash = 6.0f, float gap = 5.0f)
    {
        ImVec2      d   = ImVec2(b.x - a.x, b.y - a.y);
        const float len = std::sqrt(d.x * d.x + d.y * d.y);
        if (len < 0.001f) return;
        d.x /= len;
        d.y /= len;
        float pos = 0.0f;
        while (pos < len)
        {
            float e0 = pos;
            float e1 = std::min(pos + dash, len);
            dl->AddLine(ImVec2(a.x + d.x * e0, a.y + d.y * e0), ImVec2(a.x + d.x * e1, a.y + d.y * e1), col, thickness);
            pos += dash + gap;
        }
    }

    float Dist2(const ImVec2 &a, const ImVec2 &b)
    {
        const float dx = a.x - b.x, dy = a.y - b.y;
        return dx * dx + dy * dy;
    }

    // 新規カーブ用の色パレット（IM_COL32）
    ImU32 PaletteColor(uint32_t i)
    {
        static const ImU32 palette[] = {
            IM_COL32(0, 255, 128, 255),   // green
            IM_COL32(255, 90, 90, 255),   // red
            IM_COL32(90, 170, 255, 255),  // blue
            IM_COL32(255, 200, 60, 255),  // yellow
            IM_COL32(200, 120, 255, 255), // purple
            IM_COL32(60, 230, 230, 255),  // cyan
            IM_COL32(255, 140, 60, 255),  // orange
        };
        return palette[i % (sizeof(palette) / sizeof(palette[0]))];
    }

    const char *kInterpNames[]  = {"定数", "線形", "3次"};
    const char *kTangentNames[] = {"自動", "フラット", "揃える", "分離"};
    const char *kExtrapNames[]  = {"定数", "線形", "繰り返し", "往復", "オフセット繰り返し"};

} // namespace

// ---------------------------------------------------------------------------
// アクセサ
// ---------------------------------------------------------------------------
CurveData *CurveEditorWindow::ActiveCurve()
{
    if (m_activeCurve >= 0 && m_activeCurve < (int)m_curves.size()) return &m_curves[m_activeCurve];
    return nullptr;
}
const CurveData *CurveEditorWindow::ActiveCurve() const
{
    if (m_activeCurve >= 0 && m_activeCurve < (int)m_curves.size()) return &m_curves[m_activeCurve];
    return nullptr;
}

// ---------------------------------------------------------------------------
// 初期化
// ---------------------------------------------------------------------------
void CurveEditorWindow::EnsureInitialized()
{
    if (!m_curves.empty()) return;

    CurveData c;
    c.name  = "Curve";
    c.color = PaletteColor(m_paletteIndex++);
    c.AddKey(0.0f, 0.0f);
    c.AddKey(1.0f, 1.0f);
    m_curves.push_back(std::move(c));
    m_activeCurve = 0;
    m_selectedKeys.clear();
}

void CurveEditorWindow::AddCurve()
{
    CurveData c;
    char      name[32];
    std::snprintf(name, sizeof(name), "Curve %d", (int)m_curves.size());
    c.name  = name;
    c.color = PaletteColor(m_paletteIndex++);
    c.AddKey(0.0f, 0.0f);
    c.AddKey(1.0f, 1.0f);
    m_curves.push_back(std::move(c));
    m_activeCurve = (int)m_curves.size() - 1;
    m_selectedKeys.clear();
}

// ---------------------------------------------------------------------------
// メイン
// ---------------------------------------------------------------------------
void CurveEditorWindow::DrawContents(EditorContext &context)
{
    EnsureInitialized();

    DrawToolbar();
    ImGui::Separator();

    // 左: カーブ一覧 / 右: キャンバス＋インスペクタ
    const float listW = 190.0f;
    ImGui::BeginChild("##CurveList", ImVec2(listW, 0), true);
    DrawCurveList();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##CurveRight", ImVec2(0, 0), false);
    {
        ImVec2      avail      = ImGui::GetContentRegionAvail();
        const float inspectorH = 180.0f;
        const float canvasH    = std::max(140.0f, avail.y - inspectorH);
        DrawCanvas(canvasH);
        ImGui::Separator();
        DrawInspector();
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// ツールバー
// ---------------------------------------------------------------------------
void CurveEditorWindow::DrawToolbar()
{
    if (ImGui::Button("新規"))
    {
        m_curves.clear();
        m_selectedKeys.clear();
        m_currentFilePath.clear();
        m_paletteIndex = 0;
        EnsureInitialized();
    }

    ImGui::SameLine();
    if (ImGui::Button("読込"))
    {
        if (auto pathOpt = FileDialog::OpenPreset("CurveData", "カーブデータを読み込む"))
        {
            std::string rel = std::filesystem::relative(*pathOpt, std::filesystem::current_path()).string();
            if (LoadFromFile(rel)) m_currentFilePath = rel;
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("保存"))
    {
        if (m_currentFilePath.empty())
        {
            if (auto pathOpt = FileDialog::SavePreset("CurveData", "カーブデータを保存", "NewCurve.json"))
                m_currentFilePath = std::filesystem::relative(*pathOpt, std::filesystem::current_path()).string();
        }
        if (!m_currentFilePath.empty()) SaveToFile(m_currentFilePath);
    }

    ImGui::SameLine();
    if (ImGui::Button("名前を付けて保存"))
    {
        if (auto pathOpt = FileDialog::SavePreset("CurveData", "カーブデータを保存", "NewCurve.json"))
        {
            m_currentFilePath = std::filesystem::relative(*pathOpt, std::filesystem::current_path()).string();
            SaveToFile(m_currentFilePath);
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("全体表示")) FrameAll();

    ImGui::SameLine();
    ImGui::Checkbox("プレビュー", &m_showPreview);
    if (m_showPreview)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::DragFloat("t##preview", &m_previewTime, 0.01f);
        if (const CurveData *c = ActiveCurve())
        {
            ImGui::SameLine();
            ImGui::Text("= %.4f", c->Evaluate(m_previewTime));
        }
    }

    ImGui::SameLine();
    ImGui::TextDisabled("| %s", m_currentFilePath.empty() ? "[ 未保存 ]" : m_currentFilePath.c_str());

    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f),
                       "キャンバス: ダブルクリック=キー追加 / ドラッグ=移動 / 右or中ドラッグ=パン / ホイール=ズーム(Ctrl:横 Shift:縦) / 右クリック=メニュー / Alt+ハンドル=接線を割る");
}

// ---------------------------------------------------------------------------
// カーブ一覧（左パネル）
// ---------------------------------------------------------------------------
void CurveEditorWindow::DrawCurveList()
{
    ImGui::Text("カーブ一覧");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 22.0f);
    if (ImGui::SmallButton("+")) AddCurve();
    ImGui::Separator();

    int removeIndex = -1;
    for (int i = 0; i < (int)m_curves.size(); ++i)
    {
        CurveData &c = m_curves[i];
        ImGui::PushID(i);

        // 可視トグル
        ImGui::Checkbox("##vis", &c.visible);
        ImGui::SameLine();

        // 色スウォッチ
        ImVec4 col = ImGui::ColorConvertU32ToFloat4(c.color);
        if (ImGui::ColorEdit4("##col", &col.x,
                              ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel))
        {
            c.color = ImGui::ColorConvertFloat4ToU32(col);
        }
        ImGui::SameLine();

        // 選択（アクティブ）
        bool selected = (i == m_activeCurve);
        if (ImGui::Selectable(c.name.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick))
        {
            if (m_activeCurve != i)
            {
                m_activeCurve = i;
                m_selectedKeys.clear();
            }
        }

        // 右クリックでリネーム/削除
        if (ImGui::BeginPopupContextItem("curveItemCtx"))
        {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s", c.name.c_str());
            if (ImGui::InputText("名前", buf, sizeof(buf))) c.name = buf;
            if (m_curves.size() > 1 && ImGui::MenuItem("カーブを削除")) removeIndex = i;
            ImGui::EndPopup();
        }

        ImGui::PopID();
    }

    if (removeIndex >= 0)
    {
        m_curves.erase(m_curves.begin() + removeIndex);
        m_activeCurve = std::min(m_activeCurve, (int)m_curves.size() - 1);
        m_selectedKeys.clear();
    }
}

// ---------------------------------------------------------------------------
// キャンバス（描画＋操作の中枢）
// ---------------------------------------------------------------------------
void CurveEditorWindow::DrawCanvas(float height)
{
    ImGuiIO &io = ImGui::GetIO();

    const ImVec2 canvasPos  = ImGui::GetCursorScreenPos();
    ImVec2       canvasSize = ImGui::GetContentRegionAvail();
    canvasSize.y            = std::max(80.0f, height);
    if (canvasSize.x < 50.0f) canvasSize.x = 50.0f;

    const ImVec2 canvasEnd = ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y);

    // 座標変換 -------------------------------------------------------------
    float spanX = viewMax.x - viewMin.x;
    float spanY = viewMax.y - viewMin.y;
    if (spanX < 1e-6f) spanX = 1e-6f;
    if (spanY < 1e-6f) spanY = 1e-6f;

    auto ToScreen = [&](float t, float v) -> ImVec2 {
        float x = canvasPos.x + (t - viewMin.x) / spanX * canvasSize.x;
        float y = canvasPos.y + canvasSize.y - (v - viewMin.y) / spanY * canvasSize.y;
        return ImVec2(x, y);
    };
    auto ToCurve = [&](ImVec2 s) -> ImVec2 {
        float t = viewMin.x + (s.x - canvasPos.x) / canvasSize.x * spanX;
        float v = viewMin.y + (canvasPos.y + canvasSize.y - s.y) / canvasSize.y * spanY;
        return ImVec2(t, v);
    };

    // 入力受付用の透明ボタン（左ドラッグ/ホバー/アクティブ検出）
    ImGui::InvisibleButton("##curveCanvas", canvasSize);
    const bool hovered     = ImGui::IsItemHovered();
    const bool activated   = ImGui::IsItemActivated();   // 左押下フレーム
    const bool active      = ImGui::IsItemActive();      // 左保持中
    const bool deactivated = ImGui::IsItemDeactivated(); // 左離しフレーム

    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(canvasPos, canvasEnd, true);

    // 背景 ----------------------------------------------------------------
    dl->AddRectFilled(canvasPos, canvasEnd, IM_COL32(24, 24, 27, 255));

    CurveData *curve = ActiveCurve();

    // --- パン（右 or 中ドラッグ） ---
    const bool panning = hovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 2.0f) ||
                                     ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 2.0f));
    if (panning)
    {
        ImVec2 d   = io.MouseDelta;
        float  dcx = d.x / canvasSize.x * spanX;
        float  dcy = d.y / canvasSize.y * spanY;
        viewMin.x -= dcx;
        viewMax.x -= dcx;
        viewMin.y += dcy; // Y反転
        viewMax.y += dcy;
        spanX = viewMax.x - viewMin.x;
        spanY = viewMax.y - viewMin.y;
    }

    // --- ズーム（ホイール、カーソル位置中心） ---
    if (hovered && io.MouseWheel != 0.0f)
    {
        const float  zoom  = std::pow(1.1f, -io.MouseWheel);
        const ImVec2 pivot = ToCurve(io.MousePos);
        const bool   onlyX = io.KeyCtrl && !io.KeyShift;
        const bool   onlyY = io.KeyShift && !io.KeyCtrl;

        if (!onlyY)
        {
            viewMin.x = pivot.x + (viewMin.x - pivot.x) * zoom;
            viewMax.x = pivot.x + (viewMax.x - pivot.x) * zoom;
        }
        if (!onlyX)
        {
            viewMin.y = pivot.y + (viewMin.y - pivot.y) * zoom;
            viewMax.y = pivot.y + (viewMax.y - pivot.y) * zoom;
        }
        spanX = viewMax.x - viewMin.x;
        spanY = viewMax.y - viewMin.y;
    }

    // グリッド & 目盛り ----------------------------------------------------
    {
        const float stepX = NiceStep(spanX, canvasSize.x / 80.0f);
        const float stepY = NiceStep(spanY, canvasSize.y / 45.0f);

        // 縦線（時間軸）
        float startX = std::ceil(viewMin.x / stepX) * stepX;
        for (float gx = startX; gx <= viewMax.x; gx += stepX)
        {
            ImVec2 p    = ToScreen(gx, viewMin.y);
            bool   axis = std::fabs(gx) < stepX * 0.001f;
            dl->AddLine(ImVec2(p.x, canvasPos.y), ImVec2(p.x, canvasEnd.y),
                        axis ? IM_COL32(120, 120, 120, 160) : IM_COL32(255, 255, 255, 18), axis ? 1.5f : 1.0f);
            char buf[32];
            FormatTick(buf, sizeof(buf), gx, stepX);
            dl->AddText(ImVec2(p.x + 3, canvasEnd.y - 16), IM_COL32(170, 170, 175, 255), buf);
        }

        // 横線（値軸）
        float startY = std::ceil(viewMin.y / stepY) * stepY;
        for (float gy = startY; gy <= viewMax.y; gy += stepY)
        {
            ImVec2 p    = ToScreen(viewMin.x, gy);
            bool   axis = std::fabs(gy) < stepY * 0.001f;
            dl->AddLine(ImVec2(canvasPos.x, p.y), ImVec2(canvasEnd.x, p.y),
                        axis ? IM_COL32(120, 120, 120, 160) : IM_COL32(255, 255, 255, 18), axis ? 1.5f : 1.0f);
            char buf[32];
            FormatTick(buf, sizeof(buf), gy, stepY);
            dl->AddText(ImVec2(canvasPos.x + 3, p.y - 14), IM_COL32(170, 170, 175, 255), buf);
        }
    }

    // カーブ本体の描画 -----------------------------------------------------
    const float px = 2.0f; // サンプリング間隔（スクリーンpx）
    for (int ci = 0; ci < (int)m_curves.size(); ++ci)
    {
        const CurveData &c = m_curves[ci];
        if (!c.visible || c.empty()) continue;
        const bool isActive = (ci == m_activeCurve);

        ImVec4 base = ImGui::ColorConvertU32ToFloat4(c.color);
        ImU32  lineCol =
            IM_COL32((int)(base.x * 255), (int)(base.y * 255), (int)(base.z * 255), isActive ? 255 : 90);
        const float thick = isActive ? 2.2f : 1.4f;

        const float firstT = c.MinTime();
        const float lastT  = c.MaxTime();

        ImVec2 prev = ToScreen(viewMin.x, c.Evaluate(viewMin.x));
        for (float sx = canvasPos.x + px; sx <= canvasEnd.x; sx += px)
        {
            float  t    = ToCurve(ImVec2(sx, 0)).x;
            ImVec2 cur  = ToScreen(t, c.Evaluate(t));
            bool   extr = (t < firstT) || (t > lastT);
            if (extr)
                AddDashedLine(dl, prev, cur, IM_COL32((int)(base.x * 255), (int)(base.y * 255), (int)(base.z * 255), isActive ? 150 : 60), 1.4f);
            else
                dl->AddLine(prev, cur, lineCol, thick);
            prev = cur;
        }
    }

    // ここからアクティブカーブのキー/ハンドルの描画＆操作 ------------------
    const float keyR      = 4.5f;
    const float handleLen = 42.0f;

    // ハンドルのスクリーン位置を求める（in/out）
    auto HandlePos = [&](const CurveKeyframe &k, bool out) -> ImVec2 {
        ImVec2 kp    = ToScreen(k.time, k.value);
        float  slope = out ? k.outTangent : k.inTangent;
        // カーブ空間 (1, slope) をスクリーン方向へ
        float  dxS = canvasSize.x / spanX;      // 1時間あたりのpx
        float  dyS = canvasSize.y / spanY;      // 1値あたりのpx
        ImVec2 dir = ImVec2(dxS, -slope * dyS); // Y反転
        float  len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len < 1e-4f)
        {
            dir = ImVec2(1, 0);
            len = 1;
        }
        dir.x /= len;
        dir.y /= len;
        float sign = out ? 1.0f : -1.0f;
        return ImVec2(kp.x + dir.x * handleLen * sign, kp.y + dir.y * handleLen * sign);
    };

    // 接線ハンドルの表示可否（ヒットテスト・描画・ドラッグで共通利用）
    // OUTハンドル: このキーが最終キーでなく、かつ右セグメントがCubicのとき
    //   （右セグメントは keys[idx].interp が支配する）
    // INハンドル : このキーが先頭キーでなく、かつ左セグメントがCubicのとき
    //   （左セグメントは keys[idx-1].interp が支配する）
    auto ShowOut = [&](int idx) -> bool {
        if (!curve || idx < 0 || idx >= (int)curve->keys.size() - 1) return false;
        return curve->keys[idx].interp == CurveInterp::Cubic;
    };
    auto ShowIn = [&](int idx) -> bool {
        if (!curve || idx <= 0) return false;
        return curve->keys[idx - 1].interp == CurveInterp::Cubic;
    };

    // --- ヒットテスト用の探索 ---
    int      hoverKeyIdx      = -1;
    int      hoverHandle      = 0; // -1:in, +1:out, 0:none
    uint32_t hoverHandleKeyId = 0;

    if (curve)
    {
        // まず選択キーのハンドル（優先度高）
        if (hovered)
        {
            for (uint32_t id : m_selectedKeys)
            {
                int idx = curve->IndexOfId(id);
                if (idx < 0) continue;
                const CurveKeyframe &k = curve->keys[idx];
                if (ShowIn(idx))
                {
                    if (Dist2(io.MousePos, HandlePos(k, false)) < 8.0f * 8.0f)
                    {
                        hoverHandle      = -1;
                        hoverHandleKeyId = id;
                    }
                }
                if (ShowOut(idx))
                {
                    if (Dist2(io.MousePos, HandlePos(k, true)) < 8.0f * 8.0f)
                    {
                        hoverHandle      = 1;
                        hoverHandleKeyId = id;
                    }
                }
            }
        }
        // 次にキー本体
        if (hovered && hoverHandle == 0)
        {
            float best = 9.0f * 9.0f;
            for (int i = 0; i < (int)curve->keys.size(); ++i)
            {
                float d2 = Dist2(io.MousePos, ToScreen(curve->keys[i].time, curve->keys[i].value));
                if (d2 < best)
                {
                    best        = d2;
                    hoverKeyIdx = i;
                }
            }
        }
    }

    // --- 左ドラッグ開始 ---
    if (curve && activated)
    {
        // ダブルクリック（空白）→ キー追加
        if (ImGui::IsMouseDoubleClicked(0) && hoverKeyIdx < 0 && hoverHandle == 0)
        {
            ImVec2 cp  = ToCurve(io.MousePos);
            int    idx = curve->AddKey(cp.x, cp.y);
            m_selectedKeys.clear();
            m_selectedKeys.insert(curve->keys[idx].id);
        }
        else if (hoverHandle != 0)
        {
            m_drag         = (hoverHandle < 0) ? DragMode::TangentIn : DragMode::TangentOut;
            m_tangentKeyId = hoverHandleKeyId;
        }
        else if (hoverKeyIdx >= 0)
        {
            uint32_t id = curve->keys[hoverKeyIdx].id;
            if (io.KeyCtrl)
            {
                if (m_selectedKeys.count(id))
                    m_selectedKeys.erase(id);
                else
                    m_selectedKeys.insert(id);
            }
            else if (!m_selectedKeys.count(id))
            {
                m_selectedKeys.clear();
                m_selectedKeys.insert(id);
            }
            // 移動開始スナップショット
            m_drag           = DragMode::MoveKeys;
            m_dragStartCurve = ToCurve(io.MousePos);
            m_dragOrig.clear();
            for (uint32_t sid : m_selectedKeys)
            {
                int si = curve->IndexOfId(sid);
                if (si >= 0) m_dragOrig[sid] = ImVec2(curve->keys[si].time, curve->keys[si].value);
            }
        }
        else
        {
            // 空白 → 矩形選択
            m_drag           = DragMode::BoxSelect;
            m_boxStartScreen = io.MousePos;
            if (!io.KeyCtrl) m_selectedKeys.clear();
        }
    }

    // --- 左ドラッグ中 ---
    if (curve && active && m_drag != DragMode::None)
    {
        ImVec2 cur = ToCurve(io.MousePos);

        if (m_drag == DragMode::MoveKeys)
        {
            ImVec2 delta = ImVec2(cur.x - m_dragStartCurve.x, cur.y - m_dragStartCurve.y);
            for (auto &pr : m_dragOrig)
            {
                int si = curve->IndexOfId(pr.first);
                if (si < 0) continue;
                float nt = pr.second.x + delta.x;
                float nv = pr.second.y + delta.y;
                if (io.KeyCtrl) // スナップ
                {
                    float sx = NiceStep(spanX, canvasSize.x / 80.0f) * 0.5f;
                    float sy = NiceStep(spanY, canvasSize.y / 45.0f) * 0.5f;
                    if (sx > 0) nt = std::round(nt / sx) * sx;
                    if (sy > 0) nv = std::round(nv / sy) * sy;
                }
                curve->keys[si].time  = nt;
                curve->keys[si].value = nv;
            }
            curve->SortByTime();
            curve->RecalculateTangents();
        }
        else if (m_drag == DragMode::TangentIn || m_drag == DragMode::TangentOut)
        {
            int idx = curve->IndexOfId(m_tangentKeyId);
            if (idx >= 0)
            {
                CurveKeyframe &k     = curve->keys[idx];
                float          denom = cur.x - k.time;
                if (std::fabs(denom) < 1e-4f) denom = (denom < 0 ? -1e-4f : 1e-4f);
                float slope = (cur.y - k.value) / denom;

                const bool broken = io.KeyAlt;
                if (broken)
                {
                    if (m_drag == DragMode::TangentIn)
                    {
                        k.inTangent = slope;
                        k.tangentIn = TangentMode::Broken;
                    }
                    else
                    {
                        k.outTangent = slope;
                        k.tangentOut = TangentMode::Broken;
                    }
                }
                else
                {
                    k.inTangent  = slope;
                    k.outTangent = slope;
                    k.tangentIn  = TangentMode::Aligned;
                    k.tangentOut = TangentMode::Aligned;
                }
            }
        }
        else if (m_drag == DragMode::BoxSelect)
        {
            // 矩形をプレビュー描画
            ImVec2 a = m_boxStartScreen, b = io.MousePos;
            dl->AddRectFilled(ImVec2(std::min(a.x, b.x), std::min(a.y, b.y)),
                              ImVec2(std::max(a.x, b.x), std::max(a.y, b.y)), IM_COL32(120, 170, 255, 40));
            dl->AddRect(ImVec2(std::min(a.x, b.x), std::min(a.y, b.y)),
                        ImVec2(std::max(a.x, b.x), std::max(a.y, b.y)), IM_COL32(120, 170, 255, 200));
        }
    }

    // --- 左ドラッグ終了 ---
    if (curve && deactivated && m_drag != DragMode::None)
    {
        if (m_drag == DragMode::BoxSelect)
        {
            ImVec2 a = m_boxStartScreen, b = io.MousePos;
            float  x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x);
            float  y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
            for (const auto &k : curve->keys)
            {
                ImVec2 sp = ToScreen(k.time, k.value);
                if (sp.x >= x0 && sp.x <= x1 && sp.y >= y0 && sp.y <= y1) m_selectedKeys.insert(k.id);
            }
        }
        m_drag = DragMode::None;
        m_dragOrig.clear();
    }

    // --- Deleteキー ---
    if (hovered && !m_selectedKeys.empty())
    {
#if defined(IMGUI_VERSION_NUM) && IMGUI_VERSION_NUM >= 18700
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
#else
        if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Delete), false))
#endif
            DeleteSelectedKeys();
    }

    // --- 右クリック（ドラッグでなければ）→ コンテキストメニュー ---
    if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
    {
        ImVec2 dd = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
        if (std::fabs(dd.x) < 3.0f && std::fabs(dd.y) < 3.0f)
        {
            m_ctxCurvePos = ToCurve(io.MousePos);
            m_ctxKeyId    = (hoverKeyIdx >= 0 && curve) ? curve->keys[hoverKeyIdx].id : 0;
            ImGui::OpenPopup("CurveCanvasCtx");
        }
    }

    // 選択キー & ハンドルの描画（アクティブカーブ） -----------------------
    if (curve)
    {
        ImVec4 cbase = ImGui::ColorConvertU32ToFloat4(curve->color);
        for (int i = 0; i < (int)curve->keys.size(); ++i)
        {
            const CurveKeyframe &k     = curve->keys[i];
            ImVec2               kp    = ToScreen(k.time, k.value);
            bool                 isSel = m_selectedKeys.count(k.id) > 0;

            // 選択キーは接線ハンドルを表示（左/右セグメントがCubicの側のみ）
            if (isSel)
            {
                if (ShowIn(i))
                {
                    ImVec2 hp = HandlePos(k, false);
                    dl->AddLine(kp, hp, IM_COL32(200, 200, 200, 180), 1.2f);
                    dl->AddRectFilled(ImVec2(hp.x - 3, hp.y - 3), ImVec2(hp.x + 3, hp.y + 3), IM_COL32(230, 230, 120, 255));
                }
                if (ShowOut(i))
                {
                    ImVec2 hp = HandlePos(k, true);
                    dl->AddLine(kp, hp, IM_COL32(200, 200, 200, 180), 1.2f);
                    dl->AddRectFilled(ImVec2(hp.x - 3, hp.y - 3), ImVec2(hp.x + 3, hp.y + 3), IM_COL32(230, 230, 120, 255));
                }
            }

            ImU32 fill = isSel ? IM_COL32(255, 255, 255, 255)
                               : IM_COL32((int)(cbase.x * 255), (int)(cbase.y * 255), (int)(cbase.z * 255), 255);
            dl->AddCircleFilled(kp, isSel ? keyR + 1.0f : keyR, fill, 12);
            dl->AddCircle(kp, isSel ? keyR + 1.0f : keyR, IM_COL32(0, 0, 0, 220), 12, 1.5f);
        }
    }

    // プレビュー再生ヘッド --------------------------------------------------
    if (m_showPreview && curve)
    {
        ImVec2 top = ToScreen(m_previewTime, viewMax.y);
        dl->AddLine(ImVec2(top.x, canvasPos.y), ImVec2(top.x, canvasEnd.y), IM_COL32(255, 80, 80, 180), 1.5f);
        ImVec2 dot = ToScreen(m_previewTime, curve->Evaluate(m_previewTime));
        dl->AddCircle(dot, 5.0f, IM_COL32(255, 80, 80, 255), 12, 2.0f);
    }

    // カーソル座標の表示
    if (hovered)
    {
        ImVec2 cp = ToCurve(io.MousePos);
        char   buf[64];
        std::snprintf(buf, sizeof(buf), "t=%.3f  v=%.3f", cp.x, cp.y);
        dl->AddText(ImVec2(canvasEnd.x - 150, canvasPos.y + 4), IM_COL32(160, 160, 165, 255), buf);
    }

    dl->PopClipRect();

    // コンテキストメニュー本体（クリップ外で）
    if (ImGui::BeginPopup("CurveCanvasCtx"))
    {
        if (ImGui::MenuItem("ここにキーを追加") && curve)
        {
            int idx = curve->AddKey(m_ctxCurvePos.x, m_ctxCurvePos.y);
            m_selectedKeys.clear();
            m_selectedKeys.insert(curve->keys[idx].id);
        }
        if (m_ctxKeyId != 0 && !m_selectedKeys.count(m_ctxKeyId))
        {
            m_selectedKeys.clear();
            m_selectedKeys.insert(m_ctxKeyId);
        }

        const bool hasSel = !m_selectedKeys.empty();
        if (ImGui::BeginMenu("補間", hasSel && curve))
        {
            for (int m = 0; m < 3; ++m)
                if (ImGui::MenuItem(kInterpNames[m]))
                    for (uint32_t id : m_selectedKeys)
                    {
                        int si = curve->IndexOfId(id);
                        if (si >= 0) curve->keys[si].interp = (CurveInterp)m;
                    }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("接線", hasSel && curve))
        {
            for (int m = 0; m < 4; ++m)
                if (ImGui::MenuItem(kTangentNames[m]))
                    for (uint32_t id : m_selectedKeys)
                    {
                        int si = curve->IndexOfId(id);
                        if (si >= 0)
                        {
                            curve->keys[si].tangentIn  = (TangentMode)m;
                            curve->keys[si].tangentOut = (TangentMode)m;
                        }
                    }
            if (curve) curve->RecalculateTangents();
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("選択を削除", nullptr, false, hasSel)) DeleteSelectedKeys();
        if (ImGui::MenuItem("全体表示")) FrameAll();
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------------------
// インスペクタ（選択キー & カーブ設定）
// ---------------------------------------------------------------------------
void CurveEditorWindow::DrawInspector()
{
    ImGui::BeginChild("##CurveInspector");

    CurveData *curve = ActiveCurve();
    if (!curve)
    {
        ImGui::TextDisabled("アクティブなカーブがありません。");
        ImGui::EndChild();
        return;
    }

    // --- カーブ全体の設定 ---
    if (ImGui::CollapsingHeader("カーブ設定", ImGuiTreeNodeFlags_DefaultOpen))
    {
        char nameBuf[64];
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", curve->name.c_str());
        if (ImGui::InputText("名前", nameBuf, sizeof(nameBuf))) curve->name = nameBuf;

        int pre = (int)curve->preInfinity;
        if (ImGui::Combo("開始側の外挿", &pre, kExtrapNames, 5)) curve->preInfinity = (ExtrapMode)pre;
        int post = (int)curve->postInfinity;
        if (ImGui::Combo("終了側の外挿", &post, kExtrapNames, 5)) curve->postInfinity = (ExtrapMode)post;
    }

    ImGui::Separator();

    // --- 選択キー ---
    if (m_selectedKeys.empty())
    {
        ImGui::TextDisabled("キー未選択。ダブルクリックでキー追加、クリックで選択。");
        ImGui::EndChild();
        return;
    }

    if (m_selectedKeys.size() > 1)
    {
        ImGui::Text("%d 個のキーを選択中", (int)m_selectedKeys.size());
        // 複数選択時は補間/接線の一括変更のみ
        static int interpSel = 1;
        if (ImGui::Combo("補間 (全選択)", &interpSel, kInterpNames, 3))
            for (uint32_t id : m_selectedKeys)
            {
                int si = curve->IndexOfId(id);
                if (si >= 0) curve->keys[si].interp = (CurveInterp)interpSel;
            }
        static int tanSel = 0;
        if (ImGui::Combo("接線 (全選択)", &tanSel, kTangentNames, 4))
        {
            for (uint32_t id : m_selectedKeys)
            {
                int si = curve->IndexOfId(id);
                if (si >= 0)
                {
                    curve->keys[si].tangentIn  = (TangentMode)tanSel;
                    curve->keys[si].tangentOut = (TangentMode)tanSel;
                }
            }
            curve->RecalculateTangents();
        }
        if (ImGui::Button("選択を削除")) DeleteSelectedKeys();
        ImGui::EndChild();
        return;
    }

    // --- 単一キー詳細 ---
    uint32_t id  = *m_selectedKeys.begin();
    int      idx = curve->IndexOfId(id);
    if (idx < 0)
    {
        m_selectedKeys.clear();
        ImGui::EndChild();
        return;
    }

    CurveKeyframe &key = curve->keys[idx];
    ImGui::Text("キー [%d]", idx);

    bool timeChanged = ImGui::DragFloat("時間", &key.time, 0.01f);
    ImGui::DragFloat("値", &key.value, 0.01f);

    int interp = (int)key.interp;
    if (ImGui::Combo("補間", &interp, kInterpNames, 3)) key.interp = (CurveInterp)interp;

    if (key.interp == CurveInterp::Cubic)
    {
        int tin = (int)key.tangentIn;
        if (ImGui::Combo("入接線", &tin, kTangentNames, 4))
        {
            key.tangentIn = (TangentMode)tin;
            curve->RecalculateTangents();
        }
        int tout = (int)key.tangentOut;
        if (ImGui::Combo("出接線", &tout, kTangentNames, 4))
        {
            key.tangentOut = (TangentMode)tout;
            curve->RecalculateTangents();
        }

        // 手動モードのときだけ数値編集を許可
        bool manualIn  = (key.tangentIn == TangentMode::Aligned || key.tangentIn == TangentMode::Broken);
        bool manualOut = (key.tangentOut == TangentMode::Aligned || key.tangentOut == TangentMode::Broken);
        if (manualIn && ImGui::DragFloat("入接線(値)", &key.inTangent, 0.05f))
            if (key.tangentIn == TangentMode::Aligned)
            {
                key.outTangent = key.inTangent;
                key.tangentOut = TangentMode::Aligned;
            }
        if (manualOut && ImGui::DragFloat("出接線(値)", &key.outTangent, 0.05f))
            if (key.tangentOut == TangentMode::Aligned)
            {
                key.inTangent = key.outTangent;
                key.tangentIn = TangentMode::Aligned;
            }
    }

    if (ImGui::Button("キーを削除"))
    {
        DeleteSelectedKeys();
    }
    else if (timeChanged)
    {
        curve->SortByTime();
        curve->RecalculateTangents();
    }

    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// 操作の実装
// ---------------------------------------------------------------------------
void CurveEditorWindow::DeleteSelectedKeys()
{
    CurveData *curve = ActiveCurve();
    if (!curve) return;
    curve->keys.erase(std::remove_if(curve->keys.begin(), curve->keys.end(),
                                     [&](const CurveKeyframe &k) { return m_selectedKeys.count(k.id) > 0; }),
                      curve->keys.end());
    m_selectedKeys.clear();
    curve->RecalculateTangents();
}

void CurveEditorWindow::FrameAll()
{
    const CurveData *curve = ActiveCurve();
    if (!curve || curve->empty())
    {
        viewMin = ImVec2(-0.1f, -0.2f);
        viewMax = ImVec2(1.1f, 1.2f);
        return;
    }
    float tMin = curve->MinTime();
    float tMax = curve->MaxTime();
    float vMin, vMax;
    curve->ValueRange(vMin, vMax);

    float tPad = std::max(0.1f, (tMax - tMin) * 0.1f);
    float vPad = std::max(0.1f, (vMax - vMin) * 0.15f);
    viewMin    = ImVec2(tMin - tPad, vMin - vPad);
    viewMax    = ImVec2(tMax + tPad, vMax + vPad);
}

// ---------------------------------------------------------------------------
// シリアライズ（curves配列フォーマット）
// ---------------------------------------------------------------------------
void CurveEditorWindow::SaveToFile(const std::string &path)
{
    try
    {
        nlohmann::json j;
        j["name"]   = m_docName;
        j["curves"] = nlohmann::json::array();

        for (const auto &c : m_curves)
        {
            nlohmann::json jc;
            jc["name"]         = c.name;
            jc["color"]        = c.color;
            jc["visible"]      = c.visible;
            jc["preInfinity"]  = (int)c.preInfinity;
            jc["postInfinity"] = (int)c.postInfinity;
            jc["keys"]         = nlohmann::json::array();
            for (const auto &k : c.keys)
            {
                jc["keys"].push_back({{"time", k.time},
                                      {"value", k.value},
                                      {"inTangent", k.inTangent},
                                      {"outTangent", k.outTangent},
                                      {"interp", (int)k.interp},
                                      {"tangentIn", (int)k.tangentIn},
                                      {"tangentOut", (int)k.tangentOut}});
            }
            j["curves"].push_back(jc);
        }

        if (FileSystem::WriteAllText(path, j.dump(4)))
            CCL_LOG_SUCCESS(LogCategory::Editor, "カーブを保存しました: %s", path.c_str());
        else
            CCL_LOG_ERROR(LogCategory::Editor, "カーブの保存に失敗しました: %s", path.c_str());
    }
    catch (const std::exception &e)
    {
        CCL_LOG_ERROR(LogCategory::Editor, "Failed to save curve data: %s", e.what());
    }
}

bool CurveEditorWindow::LoadFromFile(const std::string &path)
{
    std::string jsonStr = FileSystem::ReadAllText(path);
    if (jsonStr.empty()) return false;

    try
    {
        auto j = nlohmann::json::parse(jsonStr);

        auto ReadKeys = [](const nlohmann::json &jkeys, CurveData &out) {
            for (const auto &item : jkeys)
            {
                CurveKeyframe k;
                k.time       = item.value("time", 0.0f);
                k.value      = item.value("value", 0.0f);
                k.inTangent  = item.value("inTangent", 0.0f);
                k.outTangent = item.value("outTangent", 0.0f);
                k.interp     = (CurveInterp)item.value("interp", (int)CurveInterp::Cubic);
                k.tangentIn  = (TangentMode)item.value("tangentIn", (int)TangentMode::Auto);
                k.tangentOut = (TangentMode)item.value("tangentOut", (int)TangentMode::Auto);
                out.keys.push_back(k);
            }
            out.SortByTime();
            out.AssignMissingIds();
            out.RecalculateTangents();
        };

        m_curves.clear();
        m_docName = j.value("name", "カーブセット");

        if (j.contains("curves") && j["curves"].is_array())
        {
            for (const auto &jc : j["curves"])
            {
                CurveData c;
                c.name         = jc.value("name", "Curve");
                c.color        = jc.value("color", (uint32_t)PaletteColor(m_paletteIndex));
                c.visible      = jc.value("visible", true);
                c.preInfinity  = (ExtrapMode)jc.value("preInfinity", (int)ExtrapMode::Constant);
                c.postInfinity = (ExtrapMode)jc.value("postInfinity", (int)ExtrapMode::Constant);
                if (jc.contains("keys")) ReadKeys(jc["keys"], c);
                m_curves.push_back(std::move(c));
                m_paletteIndex++;
            }
        }

        if (m_curves.empty())
        {
            CCL_LOG_ERROR(LogCategory::Editor, "Curve file had no curves: %s", path.c_str());
            EnsureInitialized();
            return false;
        }

        m_activeCurve = 0;
        m_selectedKeys.clear();
        FrameAll();
        CCL_LOG_INFO(LogCategory::Editor, "Loaded Curve: %s (%d curves)", path.c_str(), (int)m_curves.size());
        return true;
    }
    catch (const std::exception &e)
    {
        CCL_LOG_ERROR(LogCategory::Editor, "Failed to load curve data: %s", e.what());
        return false;
    }
}