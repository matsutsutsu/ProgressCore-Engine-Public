#include "CurveEditorWidget.h"

#ifdef ENABLE_EDITOR
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

// ===========================================================================
// 内部ヘルパー
// ===========================================================================
namespace
{
const char *kInterpNames[]  = {"定数", "線形", "3次"};
const char *kTangentNames[] = {"自動", "フラット", "揃える", "分離"};
const char *kExtrapNames[]  = {"定数", "線形", "繰り返し", "往復", "オフセット繰り返し"};

inline float Dist2(const ImVec2 &a, const ImVec2 &b)
{
    float dx = a.x - b.x, dy = a.y - b.y;
    return dx * dx + dy * dy;
}

// 見やすい目盛り間隔（1,2,5 * 10^n）
float NiceStep(float span, float targetDivs)
{
    if (span <= 0.0f || targetDivs <= 0.0f) return 1.0f;
    float raw  = span / targetDivs;
    float mag  = std::pow(10.0f, std::floor(std::log10(raw)));
    float norm = raw / mag;
    float nice = (norm < 1.5f) ? 1.0f : (norm < 3.0f) ? 2.0f
                                    : (norm < 7.0f)   ? 5.0f
                                                      : 10.0f;
    return nice * mag;
}

void FormatTick(char *buf, size_t n, float v, float step)
{
    int decimals = 0;
    if (step < 1.0f) decimals = (int)std::ceil(-std::log10(step));
    decimals = std::clamp(decimals, 0, 4);
    std::snprintf(buf, n, "%.*f", decimals, v);
}

void AddDashedLine(ImDrawList *dl, const ImVec2 &a, const ImVec2 &b, ImU32 col, float thick)
{
    float dx = b.x - a.x, dy = b.y - a.y;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-3f) return;
    const float dash = 5.0f, gap = 4.0f;
    float       nx = dx / len, ny = dy / len;
    for (float d = 0.0f; d < len; d += dash + gap)
    {
        float d2 = std::min(d + dash, len);
        dl->AddLine(ImVec2(a.x + nx * d, a.y + ny * d), ImVec2(a.x + nx * d2, a.y + ny * d2), col, thick);
    }
}
} // namespace

// ===========================================================================
// 公開API
// ===========================================================================
void CurveEditorWidget::SetActiveCurve(int idx)
{
    m_activeCurve = idx;
    m_sel.clear();
}

void CurveEditorWidget::FrameAll(const std::vector<CurveData *> &curves)
{
    float minT = 1e9f, maxT = -1e9f, minV = 1e9f, maxV = -1e9f;
    bool  any = false;
    for (const CurveData *c : curves)
    {
        if (!c || c->empty()) continue;
        any  = true;
        minT = std::min(minT, c->MinTime());
        maxT = std::max(maxT, c->MaxTime());
        float lo, hi;
        c->ValueRange(lo, hi);
        minV = std::min(minV, lo);
        maxV = std::max(maxV, hi);
    }
    if (!any)
    {
        m_viewMinX = 0.0f;
        m_viewMaxX = 1.0f;
        m_viewMinY = -0.2f;
        m_viewMaxY = 1.2f;
        return;
    }
    float padX = std::max(0.05f, (maxT - minT) * 0.08f);
    float padY = std::max(0.05f, (maxV - minV) * 0.15f);
    m_viewMinX  = minT - padX;
    m_viewMaxX  = maxT + padX;
    m_viewMinY  = minV - padY;
    m_viewMaxY  = maxV + padY;
}

void CurveEditorWidget::DeleteSelected(CurveData *curve, bool &modified)
{
    if (!curve || m_sel.empty()) return;
    curve->keys.erase(
        std::remove_if(curve->keys.begin(), curve->keys.end(),
            [this](const CurveKeyframe &k) { return m_sel.count(k.id) > 0; }),
        curve->keys.end());
    m_sel.clear();
    curve->RecalculateTangents();
    modified = true;
}

bool CurveEditorWidget::Draw(const char *strId, const std::vector<CurveData *> &curves, Options &opts)
{
    if (curves.empty())
    {
        ImGui::TextDisabled("表示するカーブがありません。");
        return false;
    }
    if (m_activeCurve < 0 || m_activeCurve >= (int)curves.size()) m_activeCurve = 0;
    if (opts.fitRequested) FrameAll(curves);

    bool modified = false;
    ImGui::PushID(strId);
    modified |= DrawCanvas(strId, curves, opts);
    if (opts.showInspector) DrawInspector(curves, modified);
    ImGui::PopID();
    return modified;
}

// ===========================================================================
// キャンバス（描画＋操作の中枢）
// ===========================================================================
bool CurveEditorWidget::DrawCanvas(const char *strId, const std::vector<CurveData *> &curves, Options &opts)
{
    bool     modified = false;
    ImGuiIO &io       = ImGui::GetIO();

    // float メンバ → ローカル ImVec2（末尾で書き戻す）
    ImVec2 viewMin(m_viewMinX, m_viewMinY);
    ImVec2 viewMax(m_viewMaxX, m_viewMaxY);
    auto  &m_selectedKeys = m_sel;

    const ImVec2 canvasPos  = ImGui::GetCursorScreenPos();
    ImVec2       canvasSize = ImGui::GetContentRegionAvail();
    canvasSize.y            = std::max(80.0f, opts.height);
    if (canvasSize.x < 50.0f) canvasSize.x = 50.0f;
    const ImVec2 canvasEnd = ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y);

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

    ImGui::InvisibleButton("##curveCanvas", canvasSize);
    const bool hovered     = ImGui::IsItemHovered();
    const bool activated   = ImGui::IsItemActivated();
    const bool active      = ImGui::IsItemActive();
    const bool deactivated = ImGui::IsItemDeactivated();

    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(canvasPos, canvasEnd, true);
    dl->AddRectFilled(canvasPos, canvasEnd, IM_COL32(24, 24, 27, 255));

    CurveData *curve = (m_activeCurve >= 0 && m_activeCurve < (int)curves.size()) ? curves[m_activeCurve] : nullptr;

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
        viewMin.y += dcy;
        viewMax.y += dcy;
        spanX = viewMax.x - viewMin.x;
        spanY = viewMax.y - viewMin.y;
    }

    // --- ズーム（ホイール、カーソル中心。Ctrl:横のみ Shift:縦のみ） ---
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

    // --- グリッド & 目盛り ---
    {
        const float stepX  = NiceStep(spanX, canvasSize.x / 80.0f);
        const float stepY  = NiceStep(spanY, canvasSize.y / 45.0f);
        float       startX = std::ceil(viewMin.x / stepX) * stepX;
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

    // --- 全カーブの曲線描画（非アクティブは減光） ---
    const float px = 2.0f;
    for (int ci = 0; ci < (int)curves.size(); ++ci)
    {
        const CurveData *c = curves[ci];
        if (!c || !c->visible || c->empty()) continue;
        const bool isActive = (ci == m_activeCurve);

        ImVec4      base    = ImGui::ColorConvertU32ToFloat4(c->color);
        ImU32       lineCol = IM_COL32((int)(base.x * 255), (int)(base.y * 255), (int)(base.z * 255), isActive ? 255 : 90);
        const float thick   = isActive ? 2.2f : 1.4f;
        const float firstT  = c->MinTime();
        const float lastT   = c->MaxTime();

        ImVec2 prev = ToScreen(viewMin.x, c->Evaluate(viewMin.x));
        for (float sx = canvasPos.x + px; sx <= canvasEnd.x; sx += px)
        {
            float  t    = ToCurve(ImVec2(sx, 0)).x;
            ImVec2 cur  = ToScreen(t, c->Evaluate(t));
            bool   extr = (t < firstT) || (t > lastT);
            if (extr)
                AddDashedLine(dl, prev, cur, IM_COL32((int)(base.x * 255), (int)(base.y * 255), (int)(base.z * 255), isActive ? 150 : 60), 1.4f);
            else
                dl->AddLine(prev, cur, lineCol, thick);
            prev = cur;
        }
    }

    // --- アクティブカーブのキー/ハンドル操作 ---
    const float keyR      = 4.5f;
    const float handleLen = 42.0f;

    auto HandlePos = [&](const CurveKeyframe &k, bool out) -> ImVec2 {
        ImVec2 kp    = ToScreen(k.time, k.value);
        float  slope = out ? k.outTangent : k.inTangent;
        float  dxS   = canvasSize.x / spanX;
        float  dyS   = canvasSize.y / spanY;
        ImVec2 dir   = ImVec2(dxS, -slope * dyS);
        float  len   = std::sqrt(dir.x * dir.x + dir.y * dir.y);
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
    auto ShowOut = [&](int idx) -> bool {
        if (!curve || idx < 0 || idx >= (int)curve->keys.size() - 1) return false;
        return curve->keys[idx].interp == CurveInterp::Cubic;
    };
    auto ShowIn = [&](int idx) -> bool {
        if (!curve || idx <= 0) return false;
        return curve->keys[idx - 1].interp == CurveInterp::Cubic;
    };

    // ヒットテスト
    int      hoverKeyIdx      = -1;
    int      hoverHandle      = 0;
    uint32_t hoverHandleKeyId = 0;
    if (curve)
    {
        if (hovered)
        {
            for (uint32_t id : m_selectedKeys)
            {
                int idx = curve->IndexOfId(id);
                if (idx < 0) continue;
                const CurveKeyframe &k = curve->keys[idx];
                if (ShowIn(idx) && Dist2(io.MousePos, HandlePos(k, false)) < 8.0f * 8.0f)
                {
                    hoverHandle      = -1;
                    hoverHandleKeyId = id;
                }
                if (ShowOut(idx) && Dist2(io.MousePos, HandlePos(k, true)) < 8.0f * 8.0f)
                {
                    hoverHandle      = 1;
                    hoverHandleKeyId = id;
                }
            }
        }
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

    // 左ドラッグ開始
    if (curve && activated)
    {
        if (ImGui::IsMouseDoubleClicked(0) && hoverKeyIdx < 0 && hoverHandle == 0)
        {
            ImVec2 cp  = ToCurve(io.MousePos);
            int    idx = curve->AddKey(cp.x, cp.y);
            m_selectedKeys.clear();
            m_selectedKeys.insert(curve->keys[idx].id);
            modified = true;
        }
        else if (hoverHandle != 0)
        {
            m_drag       = (hoverHandle < 0) ? Drag::TangentIn : Drag::TangentOut;
            m_tangentKey = hoverHandleKeyId;
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
            m_drag       = Drag::MoveKeys;
            m_dragStartX = ToCurve(io.MousePos).x;
            m_dragStartY = ToCurve(io.MousePos).y;
            m_dragOrig.clear();
            for (uint32_t sid : m_selectedKeys)
            {
                int si = curve->IndexOfId(sid);
                if (si >= 0) m_dragOrig.push_back({sid, {curve->keys[si].time, curve->keys[si].value}});
            }
        }
        else
        {
            m_drag      = Drag::BoxSelect;
            m_boxStartX = io.MousePos.x;
            m_boxStartY = io.MousePos.y;
            if (!io.KeyCtrl) m_selectedKeys.clear();
        }
    }

    // 左ドラッグ中
    if (curve && active && m_drag != Drag::None)
    {
        ImVec2 cur = ToCurve(io.MousePos);
        if (m_drag == Drag::MoveKeys)
        {
            float ddx = cur.x - m_dragStartX;
            float ddy = cur.y - m_dragStartY;
            for (auto &pr : m_dragOrig)
            {
                int si = curve->IndexOfId(pr.first);
                if (si < 0) continue;
                float nt = pr.second.first + ddx;
                float nv = pr.second.second + ddy;
                if (io.KeyCtrl)
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
            modified = true;
        }
        else if (m_drag == Drag::TangentIn || m_drag == Drag::TangentOut)
        {
            int idx = curve->IndexOfId(m_tangentKey);
            if (idx >= 0)
            {
                CurveKeyframe &k     = curve->keys[idx];
                float          denom = cur.x - k.time;
                if (std::fabs(denom) < 1e-4f) denom = (denom < 0 ? -1e-4f : 1e-4f);
                float slope = (cur.y - k.value) / denom;

                if (io.KeyAlt)
                {
                    if (m_drag == Drag::TangentIn)
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
                modified = true;
            }
        }
        else if (m_drag == Drag::BoxSelect)
        {
            ImVec2 a(m_boxStartX, m_boxStartY), b = io.MousePos;
            dl->AddRectFilled(ImVec2(std::min(a.x, b.x), std::min(a.y, b.y)),
                ImVec2(std::max(a.x, b.x), std::max(a.y, b.y)), IM_COL32(120, 170, 255, 40));
            dl->AddRect(ImVec2(std::min(a.x, b.x), std::min(a.y, b.y)),
                ImVec2(std::max(a.x, b.x), std::max(a.y, b.y)), IM_COL32(120, 170, 255, 200));
        }
    }

    // 左ドラッグ終了
    if (curve && deactivated && m_drag != Drag::None)
    {
        if (m_drag == Drag::BoxSelect)
        {
            ImVec2 a(m_boxStartX, m_boxStartY), b = io.MousePos;
            float  x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x);
            float  y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
            for (const auto &k : curve->keys)
            {
                ImVec2 sp = ToScreen(k.time, k.value);
                if (sp.x >= x0 && sp.x <= x1 && sp.y >= y0 && sp.y <= y1) m_selectedKeys.insert(k.id);
            }
        }
        m_drag = Drag::None;
        m_dragOrig.clear();
    }

    // Deleteキー
    if (hovered && !m_selectedKeys.empty())
    {
#if defined(IMGUI_VERSION_NUM) && IMGUI_VERSION_NUM >= 18700
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
#else
        if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Delete), false))
#endif
            DeleteSelected(curve, modified);
    }

    // 右クリック → コンテキストメニュー
    if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
    {
        ImVec2 dd = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
        if (std::fabs(dd.x) < 3.0f && std::fabs(dd.y) < 3.0f)
        {
            ImVec2 cp = ToCurve(io.MousePos);
            m_ctxX     = cp.x;
            m_ctxY     = cp.y;
            m_ctxKey   = (hoverKeyIdx >= 0 && curve) ? curve->keys[hoverKeyIdx].id : 0;
            ImGui::OpenPopup("CurveCanvasCtx");
        }
    }

    // 選択キー & ハンドルの描画
    if (curve)
    {
        ImVec4 cbase = ImGui::ColorConvertU32ToFloat4(curve->color);
        for (int i = 0; i < (int)curve->keys.size(); ++i)
        {
            const CurveKeyframe &k     = curve->keys[i];
            ImVec2               kp    = ToScreen(k.time, k.value);
            bool                 isSel = m_selectedKeys.count(k.id) > 0;

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

    // 再生ヘッド
    if (opts.showPlayhead && curve)
    {
        ImVec2 top = ToScreen(opts.playheadX, viewMax.y);
        dl->AddLine(ImVec2(top.x, canvasPos.y), ImVec2(top.x, canvasEnd.y), IM_COL32(255, 80, 80, 180), 1.5f);
        ImVec2 dot = ToScreen(opts.playheadX, curve->Evaluate(opts.playheadX));
        dl->AddCircle(dot, 5.0f, IM_COL32(255, 80, 80, 255), 12, 2.0f);
    }

    // カーソル座標
    if (hovered)
    {
        ImVec2 cp = ToCurve(io.MousePos);
        char   buf[64];
        std::snprintf(buf, sizeof(buf), "t=%.3f  v=%.3f", cp.x, cp.y);
        dl->AddText(ImVec2(canvasEnd.x - 150, canvasPos.y + 4), IM_COL32(160, 160, 165, 255), buf);
    }

    dl->PopClipRect();

    // コンテキストメニュー本体
    if (ImGui::BeginPopup("CurveCanvasCtx"))
    {
        if (ImGui::MenuItem("ここにキーを追加") && curve)
        {
            int idx = curve->AddKey(m_ctxX, m_ctxY);
            m_selectedKeys.clear();
            m_selectedKeys.insert(curve->keys[idx].id);
            modified = true;
        }
        if (m_ctxKey != 0 && !m_selectedKeys.count(m_ctxKey))
        {
            m_selectedKeys.clear();
            m_selectedKeys.insert(m_ctxKey);
        }
        const bool hasSel = !m_selectedKeys.empty();
        if (ImGui::BeginMenu("補間", hasSel && curve))
        {
            for (int m = 0; m < 3; ++m)
                if (ImGui::MenuItem(kInterpNames[m]))
                {
                    for (uint32_t id : m_selectedKeys)
                    {
                        int si = curve->IndexOfId(id);
                        if (si >= 0) curve->keys[si].interp = (CurveInterp)m;
                    }
                    modified = true;
                }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("接線", hasSel && curve))
        {
            for (int m = 0; m < 4; ++m)
                if (ImGui::MenuItem(kTangentNames[m]))
                {
                    for (uint32_t id : m_selectedKeys)
                    {
                        int si = curve->IndexOfId(id);
                        if (si >= 0)
                        {
                            curve->keys[si].tangentIn  = (TangentMode)m;
                            curve->keys[si].tangentOut = (TangentMode)m;
                        }
                    }
                    curve->RecalculateTangents();
                    modified = true;
                }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("選択を削除", nullptr, false, hasSel)) DeleteSelected(curve, modified);
        if (ImGui::MenuItem("全体表示")) FrameAll(curves);
        ImGui::EndPopup();
    }

    // ビュー範囲を書き戻す
    m_viewMinX = viewMin.x;
    m_viewMinY = viewMin.y;
    m_viewMaxX = viewMax.x;
    m_viewMaxY = viewMax.y;
    return modified;
}

// ===========================================================================
// インスペクタ（選択キー & アクティブカーブ設定）
// ===========================================================================
void CurveEditorWidget::DrawInspector(const std::vector<CurveData *> &curves, bool &modified)
{
    CurveData *curve = (m_activeCurve >= 0 && m_activeCurve < (int)curves.size()) ? curves[m_activeCurve] : nullptr;
    if (!curve) return;

    ImGui::Spacing();

    // アクティブカーブの外挿設定
    int pre = (int)curve->preInfinity;
    ImGui::SetNextItemWidth(150);
    if (ImGui::Combo("開始側の外挿", &pre, kExtrapNames, 5))
    {
        curve->preInfinity = (ExtrapMode)pre;
        modified           = true;
    }
    ImGui::SameLine();
    int post = (int)curve->postInfinity;
    ImGui::SetNextItemWidth(150);
    if (ImGui::Combo("終了側の外挿", &post, kExtrapNames, 5))
    {
        curve->postInfinity = (ExtrapMode)post;
        modified            = true;
    }

    if (m_sel.empty())
    {
        ImGui::TextDisabled("キー未選択。ダブルクリックで追加、クリックで選択。");
        return;
    }

    if (m_sel.size() > 1)
    {
        ImGui::Text("%d 個のキーを選択中", (int)m_sel.size());
        static int interpSel = 2;
        ImGui::SetNextItemWidth(150);
        if (ImGui::Combo("補間(全選択)", &interpSel, kInterpNames, 3))
        {
            for (uint32_t id : m_sel)
            {
                int si = curve->IndexOfId(id);
                if (si >= 0) curve->keys[si].interp = (CurveInterp)interpSel;
            }
            modified = true;
        }
        return;
    }

    uint32_t id  = *m_sel.begin();
    int      idx = curve->IndexOfId(id);
    if (idx < 0)
    {
        m_sel.clear();
        return;
    }

    CurveKeyframe &key = curve->keys[idx];
    ImGui::Text("キー [%d]", idx);

    ImGui::SetNextItemWidth(120);
    bool timeChanged = ImGui::DragFloat("時間", &key.time, 0.01f);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    if (ImGui::DragFloat("値", &key.value, 0.01f)) modified = true;

    int interp = (int)key.interp;
    ImGui::SetNextItemWidth(120);
    if (ImGui::Combo("補間", &interp, kInterpNames, 3))
    {
        key.interp = (CurveInterp)interp;
        modified   = true;
    }

    if (key.interp == CurveInterp::Cubic)
    {
        int tin = (int)key.tangentIn;
        ImGui::SetNextItemWidth(120);
        if (ImGui::Combo("入接線", &tin, kTangentNames, 4))
        {
            key.tangentIn = (TangentMode)tin;
            curve->RecalculateTangents();
            modified = true;
        }
        ImGui::SameLine();
        int tout = (int)key.tangentOut;
        ImGui::SetNextItemWidth(120);
        if (ImGui::Combo("出接線", &tout, kTangentNames, 4))
        {
            key.tangentOut = (TangentMode)tout;
            curve->RecalculateTangents();
            modified = true;
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("キーを削除"))
    {
        DeleteSelected(curve, modified);
        return;
    }

    if (timeChanged)
    {
        curve->SortByTime();
        curve->RecalculateTangents();
        modified = true;
    }
}

#endif // ENABLE_EDITOR