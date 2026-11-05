#include "Editor/Windows/Game/FormationLayerSequence.h"

#include "Game/Logics/AI/Drone/Formation/Data/FormationAsset.h"
#include "Game/Logics/AI/Drone/Formation/Data/FormationAssetMeta.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace CCL::AI::Formation;

// ---------------------------------------------------------------------------
// 時刻とコマの変換
// ---------------------------------------------------------------------------

int FormationLayerSequence::TimeToFrame(const Asset &asset, float time)
{
    if (time < asset.telegraphSeconds)
        return static_cast<int>(std::lround(time / asset.telegraphSeconds * kTelegraphFrames));

    const float progress = (time - asset.telegraphSeconds) / asset.attackSeconds;
    return kTelegraphFrames + static_cast<int>(std::lround(std::min(progress, 1.0f) * kAttackFrames));
}



float FormationLayerSequence::FrameToTime(const Asset &asset, int frame)
{
    if (frame < kTelegraphFrames)
        return static_cast<float>(frame) / kTelegraphFrames * asset.telegraphSeconds;

    return asset.telegraphSeconds + static_cast<float>(frame - kTelegraphFrames) / kAttackFrames * asset.attackSeconds;
}



// ---------------------------------------------------------------------------
// 編集の開始と書き戻し
// ---------------------------------------------------------------------------

void FormationLayerSequence::Begin(Asset &asset, int cursorFrame)
{
    m_asset  = &asset;
    m_cursor = cursorFrame;
    if (m_editIndex < 0) Sync();
}



void FormationLayerSequence::Sync()
{
    m_frames.resize(m_asset->layers.size());
    for (size_t i = 0; i < m_asset->layers.size(); ++i)
        m_frames[i] = ToFrames(m_asset->layers[i]);
}



bool FormationLayerSequence::Flush()
{
    const int index = m_editIndex >= 0 ? m_editIndex : m_releasedIndex;
    m_releasedIndex = -1;
    if (index < 0 || index >= static_cast<int>(m_asset->layers.size())) return false;

    FormationLayer &layer = m_asset->layers[index];
    const Section   sec   = SectionOf(layer);

    // 帯を自分の区間の外へは出さない（ImSequencer は左端 0 しか止めない）
    auto &[start, end] = m_frames[index];
    start              = std::clamp(start, sec.first, sec.first + sec.length - 1);
    end                = std::clamp(end, start, sec.first + sec.length - 1);

    if (m_frames[index] == ToFrames(layer)) return false;

    // 帯は「開始のコマの左端」から「終了のコマの右端」まで描かれるので、終了は +1 して戻す
    const float oldWidth = layer.windowEnd - layer.windowBegin;
    layer.windowBegin    = static_cast<float>(start - sec.first) / sec.length;
    layer.windowEnd      = static_cast<float>(end + 1 - sec.first) / sec.length;
    const float newWidth = layer.windowEnd - layer.windowBegin;

    // 動きの長さは帯の幅ではなく duration が決める（評価器は windowBegin から duration かけて進む）
    // 帯と一緒に duration も同じ比率で伸び縮みさせないと、帯を縮めても動きは縮まない
    // 比率で持つので、波の扇の開き方（duration / 帯の幅）はドラッグしても変わらない
    if (oldWidth > 0.0f) layer.duration = std::min(newWidth, layer.duration * newWidth / oldWidth);
    return true;
}



// ---------------------------------------------------------------------------
// ImSequencer へ見せる値
// ---------------------------------------------------------------------------

int FormationLayerSequence::GetItemCount() const
{
    return static_cast<int>(m_asset->layers.size());
}



void FormationLayerSequence::EndEdit()
{
    m_releasedIndex = m_editIndex;
    m_editIndex     = -1;
}



const char *FormationLayerSequence::GetItemLabel(int index) const
{
    const FormationLayer &layer = m_asset->layers[index];
    std::snprintf(m_label, sizeof(m_label), "%d. %s / %s", index + 1,
                  Meta::kLayerTypeNames[static_cast<int>(layer.type)],
                  Meta::kAxisNames[static_cast<int>(layer.axis)]);
    return m_label;
}



void FormationLayerSequence::Get(int index, int **start, int **end, int *type, unsigned int *color)
{
    // 種類ごとの帯の色。並びは LayerType の宣言順（Rotate / Translate / Shiver）
    constexpr ImU32 kTypeColors[] = {
        IM_COL32(80, 140, 230, 255),
        IM_COL32(90, 190, 120, 255),
        IM_COL32(230, 160, 60, 255),
    };

    const FormationLayer &layer = m_asset->layers[index];
    if (start) *start = &m_frames[index].first;
    if (end) *end = &m_frames[index].second;
    if (type) *type = static_cast<int>(layer.type);
    if (color)
    {
        // 再生位置が帯の中にある間だけ明るくする。今どの動きが形を動かしているかが一目で分かる
        const bool  active = (m_frames[index].first <= m_cursor && m_cursor <= m_frames[index].second + 1);
        const ImU32 base   = kTypeColors[static_cast<int>(layer.type)];
        *color             = active ? base : ((base >> 1) & 0x007F7F7F);
    }
}



void FormationLayerSequence::CustomDrawCompact(int index, ImDrawList *draw, const ImRect &rc, const ImRect &clip)
{
    // 波の本数は目安。実際の列数を出すには割付の式をここへ書き直すことになり、評価器と二重管理になる
    constexpr int kFanLines = 6;

    // rc は「フレーム -0.5 〜 最終フレーム +0.5」の範囲で渡される。一方で帯はコマの左端から、
    // しかも ImSequencer 内の子ウィンドウの余白(FramePadding)ぶん右へずれて描かれるので、両方を足して揃える
    const float cell = (rc.Max.x - rc.Min.x) / (GetFrameMax() - GetFrameMin() + 1);
    const float left = rc.Min.x + ImGui::GetStyle().FramePadding.x - 0.5f * cell;
    const auto  toX  = [&](float frame) { return left + frame * cell; };
    const float y0   = rc.Min.y + 4.0f;
    const float y1   = rc.Max.y - 3.0f;

    const FormationLayer &layer = m_asset->layers[index];
    const Section         sec   = SectionOf(layer);

    draw->PushClipRect(clip.Min, clip.Max, true);

    // 置けない側を暗くして、そこに何を置く区間かを書く。空いているだけだと、
    // 帯が 0 から置けない理由が画面から分からない
    const bool  inTelegraph = (sec.first == 0); // この帯は予告の区間に置く（＝攻撃の区間を隠す）
    const float sectionL    = toX(static_cast<float>(sec.first));
    const float sectionR    = toX(static_cast<float>(sec.first + sec.length));
    const float blockedL    = inTelegraph ? sectionR : toX(0.0f);
    const float blockedR    = inTelegraph ? toX(static_cast<float>(GetFrameMax() + 1)) : sectionL;
    draw->AddRectFilled(ImVec2(blockedL, rc.Min.y), ImVec2(blockedR, rc.Max.y), IM_COL32(0, 0, 0, 120));
    draw->AddText(ImVec2(blockedL + 4.0f, rc.Min.y + 2.0f), IM_COL32(255, 255, 255, 100),
                  inTelegraph ? "攻撃（動きの帯を置く）" : "予告（震えの帯を置く）");

    const float border = toX(static_cast<float>(kTelegraphFrames));
    draw->AddLine(ImVec2(border, rc.Min.y), ImVec2(border, rc.Max.y), IM_COL32(255, 255, 255, 60), 2.0f);

    const int   lines    = (layer.phaseSource == PhaseSource::None) ? 1 : kFanLines;
    const float width    = layer.windowEnd - layer.windowBegin;
    const float duration = std::min(layer.duration, width); // 評価器と同じ抑え方
    const float slack    = width - duration;

    for (int k = 0; k < lines; ++k)
    {
        // 評価器の start = windowBegin + seatT * (窓幅 - duration) と同じ式で引く
        const float seatT = (lines == 1) ? 0.0f : static_cast<float>(k) / (lines - 1);
        const float start = layer.windowBegin + seatT * slack;
        draw->AddLine(ImVec2(toX(sec.first + start * sec.length), y1),
                      ImVec2(toX(sec.first + (start + duration) * sec.length), y0),
                      IM_COL32(255, 255, 255, 170));
    }
    draw->PopClipRect();
}



// ---------------------------------------------------------------------------
// 補助
// ---------------------------------------------------------------------------

FormationLayerSequence::Section FormationLayerSequence::SectionOf(const Layer &layer)
{
    if (layer.type == LayerType::Shiver) return {0, kTelegraphFrames};
    return {kTelegraphFrames, kAttackFrames};
}



std::pair<int, int> FormationLayerSequence::ToFrames(const Layer &layer)
{
    const Section sec   = SectionOf(layer);
    const int     start = sec.first + static_cast<int>(std::lround(layer.windowBegin * sec.length));
    const int     end   = sec.first + static_cast<int>(std::lround(layer.windowEnd * sec.length)) - 1;
    return {start, std::max(start, end)};
}
