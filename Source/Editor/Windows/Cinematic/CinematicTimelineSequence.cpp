#include "Editor/Windows/Cinematic/CinematicTimelineSequence.h"

#include "Engine/Cinematic/CinematicScript.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

namespace
{
    // パスからファイル名（拡張子なし）だけを取り出す。行の名前が長くなりすぎないように
    std::string StemOf(const std::string &path)
    {
        return std::filesystem::path(path).stem().string();
    }
} // namespace

// ---------------------------------------------------------------------------
// 秒とコマの変換
// ---------------------------------------------------------------------------

int CinematicTimelineSequence::TimeToFrame(float time)
{
    return static_cast<int>(std::lround(time * kFramesPerSecond));
}



float CinematicTimelineSequence::FrameToTime(int frame)
{
    return static_cast<float>(frame) / kFramesPerSecond;
}



// ---------------------------------------------------------------------------
// 行と台本の項目の対応
// ---------------------------------------------------------------------------

int CinematicTimelineSequence::RowOf(const CinematicScript &script, RowKind kind, std::size_t index)
{
    // 自分より前に並ぶ種類の数を足していく（並びは Sync と同じ）
    std::size_t row = index;
    if (kind == RowKind::Shot) return static_cast<int>(row);
    row += script.shots.size();
    if (kind == RowKind::Sound) return static_cast<int>(row);
    row += script.sounds.size();
    if (kind == RowKind::Effect) return static_cast<int>(row);
    row += script.effects.size();
    if (kind == RowKind::Signal) return static_cast<int>(row);
    row += script.signals.size();
    return static_cast<int>(row);
}



bool CinematicTimelineSequence::RowAt(int row, Row &out) const
{
    if (row < 0 || row >= static_cast<int>(m_rows.size())) return false;
    out = m_rows[row];
    return true;
}



// ---------------------------------------------------------------------------
// 編集の開始と書き戻し
// ---------------------------------------------------------------------------

void CinematicTimelineSequence::Begin(CinematicScript &script, int cursorFrame, bool ripple)
{
    m_script        = &script;
    m_cursor        = cursorFrame;
    m_rippleEnabled = ripple;

    // 掴んでいない間は、台本から帯を写し直す。リップルの控えも、離した次のフレームのここで捨てる
    if (m_editIndex < 0)
    {
        m_ripple = {};
        Sync();
    }
}



void CinematicTimelineSequence::BeginEdit(int index)
{
    m_editIndex = index;

    // ショットの帯を掴んだら、後ろの項目の今の時刻を控える。ドラッグ中は、毎フレームこの控えからずらし直す
    m_ripple = {};
    if (!m_rippleEnabled || index < 0 || index >= static_cast<int>(m_rows.size())) return;
    if (m_rows[index].kind == RowKind::Shot) m_ripple = CinematicRipple::Capture(*m_script, m_rows[index].index);
}



bool CinematicTimelineSequence::Flush()
{
    // 処理フロー:
    // 1. 掴んでいる（か離したばかりの）帯を決め、全体の長さの中に収める
    // 2. 動いていたら、台本の時刻と長さへ書き戻す
    // 3. リップルなら、後ろの項目と全体の長さもずらし、その帯を写し直す

    // ---------------------------------------------------------
    // 1. 帯を決めて収める
    // ---------------------------------------------------------
    const int index = m_editIndex >= 0 ? m_editIndex : m_releasedIndex;
    m_releasedIndex = -1;
    if (index < 0 || index >= static_cast<int>(m_rows.size())) return false;

    const Row                 row    = m_rows[index];
    const std::pair<int, int> before = ToFrames(row);

    // 帯を全体の長さの外へは出さない（ImSequencer は左端 0 しか止めない）
    // 右端まで行かせないのは、全体の長さ以降に始まる項目を読み込みが書き間違いとして弾くため
    // リップル中のショットの終わりだけは外へ出してよい。全体の長さも同じだけ伸びるので、最後のショットも伸ばせる
    auto &[start, end] = m_frames[index];
    const int maxFrame = GetFrameMax();
    start              = std::clamp(start, 0, maxFrame);
    end                = m_ripple.valid ? std::max(end, start) : std::clamp(end, start, maxFrame);

    // 点は長さを持たないので、端を掴んで伸ばしても1コマ幅に戻す
    const bool isPoint = (row.kind == RowKind::Sound || row.kind == RowKind::Effect || row.kind == RowKind::Signal);
    if (isPoint) end = start;

    if (m_frames[index] == before) return false;

    // ---------------------------------------------------------
    // 2. 書き戻す
    // ---------------------------------------------------------

    // 帯は「開始のコマの左端」から「終了のコマの右端」まで描かれるので、終わりの時刻は +1 して戻す
    // 長さは幅が変わったときだけ書き戻す。丸ごと動かしただけで書くと、アニメの「長さ 0（クリップ1回分）」が数字に変わる
    const float time    = FrameToTime(start);
    const bool  resized = (end - start) != (before.second - before.first);
    const float length  = FrameToTime(end + 1) - time;

    CinematicScript &s = *m_script;
    switch (row.kind)
    {
    case RowKind::Shot:
        s.shots[row.index].start = time;
        if (resized) s.shots[row.index].duration = length;
        break;
    case RowKind::Sound: s.sounds[row.index].time = time; break;
    case RowKind::Effect: s.effects[row.index].time = time; break;
    case RowKind::Signal: s.signals[row.index].time = time; break;
    case RowKind::Animation:
        s.animations[row.index].start = time;
        if (resized) s.animations[row.index].duration = length;
        break;
    }

    // ---------------------------------------------------------
    // 3. リップル
    // ---------------------------------------------------------

    // 控えはショットの帯を掴んだときにだけあるので、ここに来るのはショットの帯を動かしたときだけ
    if (m_ripple.valid)
    {
        CinematicRipple::Apply(s, m_ripple);

        // 掴んでいる間は Begin() が写し直さないので、ずらした帯をここで写す。掴んでいる帯は ImSequencer の値のまま残す
        for (std::size_t i = 0; i < m_rows.size(); ++i)
        {
            if (static_cast<int>(i) != index) m_frames[i] = ToFrames(m_rows[i]);
        }
    }
    return true;
}



void CinematicTimelineSequence::EndEdit()
{
    m_releasedIndex = m_editIndex;
    m_editIndex     = -1;
}



void CinematicTimelineSequence::Sync()
{
    m_rows.clear();
    for (std::size_t i = 0; i < m_script->shots.size(); ++i) m_rows.push_back({RowKind::Shot, i});
    for (std::size_t i = 0; i < m_script->sounds.size(); ++i) m_rows.push_back({RowKind::Sound, i});
    for (std::size_t i = 0; i < m_script->effects.size(); ++i) m_rows.push_back({RowKind::Effect, i});
    for (std::size_t i = 0; i < m_script->signals.size(); ++i) m_rows.push_back({RowKind::Signal, i});
    for (std::size_t i = 0; i < m_script->animations.size(); ++i) m_rows.push_back({RowKind::Animation, i});

    m_frames.resize(m_rows.size());
    for (std::size_t i = 0; i < m_rows.size(); ++i) m_frames[i] = ToFrames(m_rows[i]);
}



std::pair<int, int> CinematicTimelineSequence::ToFrames(const Row &row) const
{
    // 区間は「開始のコマの左端」から「終了のコマの右端」まで描かれるので、終わりの時刻のコマから 1 引く
    auto span = [](float start, float end) {
        const int first = TimeToFrame(start);
        return std::pair<int, int>{first, std::max(first, TimeToFrame(end) - 1)};
    };
    // 点は長さがないので、1コマ幅にする
    auto point = [](float time) {
        const int frame = TimeToFrame(time);
        return std::pair<int, int>{frame, frame};
    };

    const CinematicScript &s = *m_script;
    switch (row.kind)
    {
    case RowKind::Shot: return span(s.shots[row.index].start, s.shots[row.index].start + s.shots[row.index].duration);
    case RowKind::Sound: return point(s.sounds[row.index].time);
    case RowKind::Effect: return point(s.effects[row.index].time);
    case RowKind::Signal: return point(s.signals[row.index].time);
    case RowKind::Animation: return span(s.animations[row.index].start, s.animations[row.index].End());
    }
    return {0, 0};
}



// ---------------------------------------------------------------------------
// ImSequencer へ見せる値
// ---------------------------------------------------------------------------

int CinematicTimelineSequence::GetFrameMax() const
{
    // 最後のコマは「終わりの時刻の1つ前」。終わりちょうどは再生位置としてだけ使う
    return std::max(0, TimeToFrame(m_script->duration) - 1);
}



const char *CinematicTimelineSequence::GetItemLabel(int index) const
{
    const Row             &row = m_rows[index];
    const CinematicScript &s   = *m_script;

    switch (row.kind)
    {
    case RowKind::Shot:
    {
        const CameraShot &shot = s.shots[row.index];
        std::snprintf(m_label, sizeof(m_label), "ショット%zu %s %s%s", row.index + 1,
                      kCameraMotionNames[static_cast<int>(shot.motion)], shot.subject.c_str(), shot.dof ? " DoF" : "");
        break;
    }
    case RowKind::Sound:
        std::snprintf(m_label, sizeof(m_label), "SE %s", s.sounds[row.index].event.c_str());
        break;
    case RowKind::Effect:
        std::snprintf(m_label, sizeof(m_label), "エフェクト %s", StemOf(s.effects[row.index].effect).c_str());
        break;
    case RowKind::Signal:
        std::snprintf(m_label, sizeof(m_label), "シグナル %s %s", s.signals[row.index].name.c_str(),
                      s.signals[row.index].param.c_str());
        break;
    case RowKind::Animation:
        std::snprintf(m_label, sizeof(m_label), "アニメ %s %s", s.animations[row.index].subject.c_str(),
                      StemOf(s.animations[row.index].sequence).c_str());
        break;
    }
    return m_label;
}



void CinematicTimelineSequence::Get(int index, int **start, int **end, int *type, unsigned int *color)
{
    // 種類ごとの帯の色。並びは RowKind の宣言順（ショット / SE / エフェクト / シグナル / アニメ）
    constexpr ImU32 kKindColors[] = {
        IM_COL32(80, 140, 230, 255),
        IM_COL32(90, 190, 120, 255),
        IM_COL32(230, 160, 60, 255),
        IM_COL32(200, 100, 200, 255),
        IM_COL32(220, 90, 90, 255),
    };

    const Row &row = m_rows[index];
    if (start) *start = &m_frames[index].first;
    if (end) *end = &m_frames[index].second;
    if (type) *type = static_cast<int>(row.kind);
    if (color)
    {
        // 再生位置が帯の中にある間だけ明るくする。今どの項目が効いているかが一目で分かる
        const bool  active = (m_frames[index].first <= m_cursor && m_cursor <= m_frames[index].second);
        const ImU32 base   = kKindColors[static_cast<int>(row.kind)];
        *color             = active ? base : ((base >> 1) & 0x007F7F7F);
    }
}
