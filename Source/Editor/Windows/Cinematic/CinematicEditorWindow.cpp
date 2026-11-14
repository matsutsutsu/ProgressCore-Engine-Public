#include "Editor/Windows/Cinematic/CinematicEditorWindow.h"

#include "Engine/Platform/Core/Dialog.h"

#ifdef ENABLE_EDITOR
#include "Engine/Animation/Core/AnimSequence.h"
#include "Engine/Audio/IAudioAPI.h"
#include "Engine/Cinematic/CinematicEvents.h"
#include "Engine/Cinematic/CinematicSubject.h"
#include "Engine/Cinematic/Components/CinematicCameraComponent.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Graphics/Components/PostProcess/DepthOfFieldConfigComponent.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Graphics/SceneView/Camera.h"

#include <imgui.h>
#include <SimpleMath.h>

#include "ImSequencer.h"

#include "Editor/Windows/Cinematic/CinematicTimelineSequence.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>
#endif

#ifdef ENABLE_EDITOR

using namespace CCL::ECS;
using DirectX::SimpleMath::Matrix;
using DirectX::SimpleMath::Vector3;
using RowKind = CinematicTimelineSequence::RowKind;

namespace
{
    constexpr ImVec4 kWarnColor(1.0f, 0.75f, 0.2f, 1.0f); // 注意を引く文字の色

    // --- 配置 ---
    constexpr float kInspectorWidth       = 400.0f; // 右のペインの幅
    constexpr float kLabelChars           = 9.0f;   // インスペクタで欄の右に残す名前の幅（文字数）
    constexpr float kGroupSpacing         = 24.0f;  // 上の帯で、まとまりの間に空ける幅
    constexpr float kTransportButtonWidth = 56.0f;  // 再生・停止のボタンの幅
    constexpr float kDurationFieldWidth   = 90.0f;  // 全体の長さの欄の幅

    // --- 再生 ---
    constexpr const char *kUnsavedPlaybackName    = "（新しい台本）"; // 保存前の台本を流すときの名前
    constexpr int         kPlayStartTimeoutFrames = 30;              // 再生要求を出してから、流れ始めるのを待つフレーム数



    bool InputString(const char *label, std::string &value)
    {
        char buf[256];
        strcpy_s(buf, sizeof(buf), value.c_str());
        if (!ImGui::InputText(label, buf, sizeof(buf))) return false;
        value = buf;
        return true;
    }



    bool DragSeconds(const char *label, float &value, float min = 0.0f)
    {
        return ImGui::DragFloat(label, &value, 0.01f, min, FLT_MAX, "%.2f 秒");
    }



    bool DragVec3(const char *label, DirectX::XMFLOAT3 &v)
    {
        return ImGui::DragFloat3(label, &v.x, 0.05f, 0.0f, 0.0f, "%.2f");
    }



    // 列挙値を名前の一覧（列挙と同じ順）から選ぶ欄
    template <class Enum, std::size_t N>
    bool ComboEnum(const char *label, const char *const (&names)[N], Enum &value)
    {
        int index = static_cast<int>(value);
        if (!ImGui::Combo(label, &index, names, static_cast<int>(N))) return false;
        value = static_cast<Enum>(index);
        return true;
    }



    // パスの欄。綴りを間違えると読めず原因に気付きにくいので、手で打たせずに参照ボタンで選ばせる
    // stripExt なら拡張子を落とす（エフェクトのプレハブは拡張子なしで指す）
    bool PathField(const char *label, std::string &path, const char *preset, bool stripExt)
    {
        ImGui::PushID(label);
        bool changed = false;
        if (ImGui::Button("参照..."))
        {
            if (auto picked = FileDialog::OpenPreset(preset))
            {
                std::string rel = FileDialog::ConvertToRelativePath(*picked);
                if (stripExt)
                {
                    const std::size_t dot   = rel.find_last_of('.');
                    const std::size_t slash = rel.find_last_of('/');
                    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) rel.erase(dot);
                }
                path    = rel;
                changed = true;
            }
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(path.empty() ? "（未設定）" : path.c_str());
        ImGui::SameLine();
        ImGui::TextUnformatted(label);
        ImGui::PopID();
        return changed;
    }



    // 時刻 t で映るショット（開始時刻が t 以下のうち最後）。編集中の写しは並んでいないので開始時刻で比べる
    const CameraShot *ShotAt(const std::vector<CameraShot> &shots, float t)
    {
        const CameraShot *found = nullptr;
        for (const CameraShot &s : shots)
        {
            if (s.start > t) continue;
            if (!found || s.start >= found->start) found = &s;
        }
        if (!found && !shots.empty()) found = &shots.front();
        return found;
    }



    // アニメのファイルを読み、「長さ 0（クリップ1回分）」に使うクリップの長さを入れる
    void ResolveAnimation(CinematicAnimation &anim)
    {
        anim.resolved     = anim.sequence.empty() ? nullptr : ResourceManager::Instance().LoadAnimSequence(anim.sequence.c_str());
        anim.clipDuration = anim.resolved ? anim.resolved->duration : 0.0f;
    }
} // namespace



CinematicEditorWindow::CinematicEditorWindow()
    : EditorWindow("Cinematic Editor", false), m_sequence(std::make_unique<CinematicTimelineSequence>())
{
}



CinematicEditorWindow::~CinematicEditorWindow() = default;



// ---------------------------------------------------------------------------
// 画面
// ---------------------------------------------------------------------------

void CinematicEditorWindow::DrawContents(EditorContext &context)
{
    // 新規・開く・保存の行
    DrawToolbar(context);
    ImGui::Separator();

    // 再生の操作と、全体の長さの行
    DrawTransportBar(context);

    // 今の状態の注意か、直近の結果を1行
    DrawStatusLine(context);

    // --- 左: タイムライン ---
    const float timelineWidth = -(kInspectorWidth + ImGui::GetStyle().ItemSpacing.x);
    ImGui::BeginChild("timelinePane", ImVec2(timelineWidth, 0.0f), true);

    // 項目を足すボタン。足した項目は次のタイムラインで行に並ぶ
    m_dirty |= DrawAddButtons();

    // ショットとトラックの帯
    m_dirty |= DrawTimeline();
    ImGui::EndChild();

    // --- 右: インスペクタ ---
    ImGui::SameLine();
    ImGui::BeginChild("inspectorPane", ImVec2(0.0f, 0.0f), true);

    // 選んでいる項目の数値の欄
    // タイムラインの後に描くので、行と項目の対応が作り直された後になり、足した項目を同じフレームで出せる
    m_dirty |= DrawInspector(context);
    ImGui::EndChild();
}



void CinematicEditorWindow::OnFrameBegin(EditorContext &context)
{
    // ゲームでこの台本が流れていれば、再生位置をそれに合わせる
    m_followingGame = FollowGamePlayback(context);

    // エディタから流したムービーが終わったら、一時停止へ戻すかループで流し直す
    // ウィンドウを閉じていても見るので、流している途中で閉じても一時停止へ戻せる
    UpdatePlayback(context);

    // 一時停止中だけ、再生位置の絵をゲーム画面へ出す。止めるのは出していたときの1回だけ
    const bool active = GetVisible() && m_previewEnabled && context.isPaused;
    if (active || m_previewPushed) PushPreview(context, active);
    m_previewPushed = active;
}



void CinematicEditorWindow::DrawToolbar(EditorContext &context)
{
    const bool hasFile = !m_script.path.empty();

    // --- 開く ---
    if (ImGui::Button("新規")) NewScript();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("空の台本から作る（保存していない変更は捨てる）");

    ImGui::SameLine();
    if (ImGui::Button("開く..."))
    {
        if (auto picked = FileDialog::OpenPreset("Cinematic")) Open(FileDialog::ConvertToRelativePath(*picked), false);
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(!hasFile);
    if (ImGui::Button("開き直す")) Open(m_script.path, true);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("いじった分を捨てて、保存した状態に戻す");

    // --- 保存 ---
    ImGui::SameLine(0.0f, kGroupSpacing);
    if (ImGui::Button("保存")) Save(context);

    ImGui::SameLine();
    if (ImGui::Button("名前を付けて保存...")) SaveAs(context);

    // --- 開いている台本 ---
    ImGui::SameLine(0.0f, kGroupSpacing);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(hasFile ? m_script.path.c_str() : kUnsavedPlaybackName);

    if (m_dirty)
    {
        ImGui::SameLine();
        ImGui::TextColored(kWarnColor, "未保存");
    }
}



void CinematicEditorWindow::DrawTransportBar(EditorContext &context)
{
    // --- 再生 ---
    ImGui::BeginDisabled(m_script.shots.empty());
    if (ImGui::Button(m_playing ? "停止" : "再生", ImVec2(kTransportButtonWidth, 0.0f)))
    {
        if (m_playing)
        {
            StopPlayback(context, true);
        }
        else
        {
            // 終わりで止まっていたら、頭から流す
            StartPlayback(context, (m_time >= m_script.duration) ? 0.0f : m_time);
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("再生位置から、SE・エフェクト・アニメまで本番どおりに流す（保存しなくてよい）\n"
                          "一時停止中に押すと、流し終えたところで止まる");
    }

    ImGui::SameLine();
    if (ImGui::Button("頭へ"))
    {
        m_time = 0.0f;
        if (m_playing) StartPlayback(context, 0.0f);
    }

    ImGui::SameLine();
    ImGui::Checkbox("ループ", &m_playLoop);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("終わったら、流し始めた位置から流し直す");

    // --- 再生位置と全体の長さ ---
    ImGui::SameLine(0.0f, kGroupSpacing);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%.2f /", m_time);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(kDurationFieldWidth);
    m_dirty |= DragSeconds("##duration", m_script.duration, 0.1f);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("ムービー全体の長さ");

    // --- 設定 ---
    ImGui::SameLine(0.0f, kGroupSpacing);
    m_dirty |= ImGui::Checkbox("長押しでスキップ", &m_script.allowSkip);

    ImGui::SameLine(0.0f, kGroupSpacing);
    ImGui::Checkbox("一時停止中に画面へ出す", &m_previewEnabled);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tab で止めて、再生位置のカメラの絵を見ながら構図を詰める");
}



void CinematicEditorWindow::DrawStatusLine(EditorContext &context)
{
    // 行が増えたり消えたりすると下のタイムラインが跳ねるので、いつも1行だけ描く
    if (m_playing)
    {
        ImGui::TextColored(kWarnColor, "再生中（止めると、流す前の状態に戻ります）");
        return;
    }

    if (m_followingGame)
    {
        ImGui::TextColored(kWarnColor, "ゲームで再生中（再生位置が付いていきます）");
        return;
    }

    // カメラ側は毎フレーム警告を出せないので、基準がいないことはここで知らせる
    const CameraShot *shot = ShotAt(m_script.shots, m_time);
    if (m_previewPushed && shot && !shot->subject.empty() && context.world &&
        Cinematic::FindEntityByName(*context.world, shot->subject) == InvalidEntityID)
    {
        ImGui::TextColored(kWarnColor, "ショットの基準 '%s' がシーンにいないので、画面に出せません", shot->subject.c_str());
        return;
    }

    ImGui::TextDisabled("%s", m_status.c_str());
}



bool CinematicEditorWindow::DrawAddButtons()
{
    // 全体の長さちょうどに置くと映らないので、1コマ手前までにする
    const float lastTime = std::max(0.0f, m_script.duration - 1.0f / CinematicTimelineSequence::kFramesPerSecond);
    const float at       = std::clamp(m_time, 0.0f, lastTime);

    bool        added = false;
    RowKind     kind  = RowKind::Shot;
    std::size_t index = 0;

    // --- 足す ---
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("足す:");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("再生位置に置く");

    ImGui::SameLine();
    if (ImGui::Button("ショット"))
    {
        // 一から書くより近い構図から直す方が早いので、今映っているショットを写す
        CameraShot shot;
        if (const CameraShot *base = ShotAt(m_script.shots, at)) shot = *base;
        shot.start    = at;
        shot.duration = 1.0f;
        m_script.shots.push_back(shot);

        added = true;
        kind  = RowKind::Shot;
        index = m_script.shots.size() - 1;
    }

    ImGui::SameLine();
    if (ImGui::Button("SE"))
    {
        CinematicSound sound;
        sound.time = at;
        m_script.sounds.push_back(sound);

        added = true;
        kind  = RowKind::Sound;
        index = m_script.sounds.size() - 1;
    }

    ImGui::SameLine();
    if (ImGui::Button("エフェクト"))
    {
        CinematicEffect effect;
        effect.time = at;
        m_script.effects.push_back(effect);

        added = true;
        kind  = RowKind::Effect;
        index = m_script.effects.size() - 1;
    }

    ImGui::SameLine();
    if (ImGui::Button("シグナル"))
    {
        CinematicSignal signal;
        signal.time = at;
        m_script.signals.push_back(signal);

        added = true;
        kind  = RowKind::Signal;
        index = m_script.signals.size() - 1;
    }

    ImGui::SameLine();
    if (ImGui::Button("アニメ"))
    {
        CinematicAnimation anim;
        anim.start = at;
        m_script.animations.push_back(anim);

        added = true;
        kind  = RowKind::Animation;
        index = m_script.animations.size() - 1;
    }

    // 足した項目を選んで、すぐ数値を直せるようにする
    if (added) m_selectedRow = CinematicTimelineSequence::RowOf(m_script, kind, index);

    // --- リップル ---
    ImGui::SameLine(0.0f, kGroupSpacing);
    ImGui::Checkbox("後ろも一緒にずらす", &m_rippleEnabled);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("ショットの終わりを動かすと、後ろの項目と全体の長さも同じだけずれる");

    // --- 操作の早見（右端に寄せる） ---
    const char *helpLabel = "(?)";
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                         std::max(0.0f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(helpLabel).x));
    ImGui::TextDisabled("%s", helpLabel);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("帯の中をドラッグ : 時刻をずらす\n"
                          "帯の端をドラッグ : 長さを変える（ショット・アニメだけ）\n"
                          "上の目盛りをクリック : 再生位置を動かす\n"
                          "Ctrl + ホイール : 拡大・縮小");
    }
    return added;
}



bool CinematicEditorWindow::DrawTimeline()
{
    int       frame  = CinematicTimelineSequence::TimeToFrame(m_time);
    const int before = frame;
    m_sequence->Begin(m_script, frame, m_rippleEnabled);

    // ImSequencer は項目が1つもないと何も描かない
    if (m_sequence->GetItemCount() == 0)
    {
        ImGui::Spacing();
        ImGui::TextDisabled("「足す」からショットを置くと、ここに帯が並びます");
        return false;
    }

    // ImSequencer は残りの高さを全部使うので、行数ぶんの高さに閉じ込める
    // 目盛り(20) + 行(20 x 数) + 横スクロール(14) + 余白(8) + 余裕(4)
    constexpr float kItemHeight = 20.0f;
    const float     height      = kItemHeight * (m_sequence->GetItemCount() + 1) + 14.0f + 8.0f + 4.0f;

    ImGui::BeginChild("timeline", ImVec2(0.0f, height));
    ImSequencer::Sequencer(m_sequence.get(), &frame, nullptr, &m_selectedRow, &m_firstFrame,
                           ImSequencer::SEQUENCER_EDIT_STARTEND | ImSequencer::SEQUENCER_CHANGE_FRAME);
    ImGui::EndChild();

    if (frame != before) m_time = CinematicTimelineSequence::FrameToTime(frame);
    return m_sequence->Flush();
}



bool CinematicEditorWindow::DrawInspector(EditorContext &context)
{
    CinematicTimelineSequence::Row row;
    if (!m_sequence->RowAt(m_selectedRow, row))
    {
        ImGui::TextDisabled("タイムラインの帯を選ぶと、その項目を直せます");
        return false;
    }

    // --- 名前と消すボタン ---
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(m_sequence->GetItemLabel(m_selectedRow));

    const char *eraseLabel = "消す";
    const float eraseWidth = ImGui::CalcTextSize(eraseLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - eraseWidth));
    if (ImGui::Button(eraseLabel))
    {
        EraseSelected();
        return true;
    }
    ImGui::Separator();

    // --- 数値の欄。右に名前が収まるだけ残して、幅いっぱいにする ---
    ImGui::PushItemWidth(-ImGui::GetFontSize() * kLabelChars);

    bool changed = false;
    switch (row.kind)
    {
    case RowKind::Shot:
        changed = DrawShotFields(context, row.index);
        break;
    case RowKind::Sound:
        changed = DrawSoundFields(context, m_script.sounds[row.index]);
        break;
    case RowKind::Effect:
        changed = DrawEffectFields(context, m_script.effects[row.index]);
        break;
    case RowKind::Signal:
        changed = DrawSignalFields(m_script.signals[row.index]);
        break;
    case RowKind::Animation:
        changed = DrawAnimationFields(context, m_script.animations[row.index]);
        break;
    }

    ImGui::PopItemWidth();
    return changed;
}



void CinematicEditorWindow::EraseSelected()
{
    CinematicTimelineSequence::Row row;
    if (!m_sequence->RowAt(m_selectedRow, row)) return;

    auto eraseAt = [](auto &items, std::size_t i) {
        if (i < items.size()) items.erase(items.begin() + static_cast<std::ptrdiff_t>(i));
    };

    switch (row.kind)
    {
    case RowKind::Shot:
        eraseAt(m_script.shots, row.index);
        break;
    case RowKind::Sound:
        eraseAt(m_script.sounds, row.index);
        break;
    case RowKind::Effect:
        eraseAt(m_script.effects, row.index);
        break;
    case RowKind::Signal:
        eraseAt(m_script.signals, row.index);
        break;
    case RowKind::Animation:
        eraseAt(m_script.animations, row.index);
        break;
    }
    m_selectedRow = -1;
}



// ---------------------------------------------------------------------------
// 種類ごとの数値の欄
// ---------------------------------------------------------------------------

bool CinematicEditorWindow::DrawShotFields(EditorContext &context, std::size_t shotIndex)
{
    // 処理フロー:
    // 1. 時間。リップルなら、終わりが動いた分だけ後ろもずらす
    // 2. 動き
    // 3. 構図（開始と終了をタブで切り替える）
    // 4. 被写界深度

    // リップルはほかのショットの時刻を書き換えるが、配列の大きさは変えないので、この参照は最後まで使える
    CameraShot &shot    = m_script.shots[shotIndex];
    bool        changed = false;

    // ---------------------------------------------------------
    // 1. 時間
    // ---------------------------------------------------------
    ImGui::SeparatorText("時間");

    // 触っている間は控え直さない。縮めたときに、途中にあった項目へ終わりが追いついて一緒に押してしまう
    const bool otherShot = !m_timeFieldRipple.valid || m_timeFieldRipple.shotIndex != shotIndex;
    if (m_rippleEnabled && (!m_timeFieldActive || otherShot))
    {
        m_timeFieldRipple = CinematicRipple::Capture(m_script, shotIndex);
    }

    bool timeChanged = DragSeconds("開始", shot.start);
    bool timeActive  = ImGui::IsItemActive();
    timeChanged |= DragSeconds("長さ", shot.duration, 0.01f);
    timeActive |= ImGui::IsItemActive();
    m_timeFieldActive = timeActive;

    if (timeChanged && m_rippleEnabled) CinematicRipple::Apply(m_script, m_timeFieldRipple);
    changed |= timeChanged;

    // ---------------------------------------------------------
    // 2. 動き
    // ---------------------------------------------------------
    ImGui::SeparatorText("動き");
    changed |= DrawSubjectField(context, shot.subject, "ワールドの原点");
    changed |= ComboEnum("動かし方", kCameraMotionNames, shot.motion);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Truck : 開始から終了へまっすぐ動く\nOrbit : 注視点の周りを回り込む");
    changed |= ComboEnum("速さのカーブ", kCameraEaseNames, shot.ease);

    // ---------------------------------------------------------
    // 3. 構図
    // ---------------------------------------------------------
    ImGui::SeparatorText("構図");
    const bool orbit = (shot.motion == CameraMotion::Orbit);

    if (ImGui::BeginTabBar("framing"))
    {
        for (int side = 0; side < 2; ++side)
        {
            const bool toEnd = (side == 1);
            if (!ImGui::BeginTabItem(toEnd ? "終了" : "開始")) continue;

            if (ImGui::Button("今の画面を取り込む", ImVec2(-FLT_MIN, 0.0f))) changed |= CaptureView(context, shot, toEnd);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("自由カメラで決めた構図を、そのまま入れる");

            changed |= DragVec3("注視点", toEnd ? shot.lookEnd : shot.lookStart);
            if (orbit)
            {
                changed |= ImGui::DragFloat("角度", toEnd ? &shot.angleEnd : &shot.angleStart, 0.5f, 0.0f, 0.0f, "%.1f 度");
                changed |= ImGui::DragFloat("半径", toEnd ? &shot.radiusEnd : &shot.radiusStart, 0.05f, 0.0f, FLT_MAX, "%.2f m");
                changed |= ImGui::DragFloat("高さ", toEnd ? &shot.heightEnd : &shot.heightStart, 0.05f, 0.0f, 0.0f, "%.2f m");
            }
            else
            {
                changed |= DragVec3("カメラ位置", toEnd ? shot.eyeEnd : shot.eyeStart);
            }
            changed |= ImGui::DragFloat("画角", toEnd ? &shot.fovEnd : &shot.fovStart, 0.1f, 1.0f, 179.0f, "%.1f 度");

            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::TextDisabled(orbit ? "角度 0 = 前 / 90 = 右（基準の向きから見て）"
                              : "+X = 右 / +Y = 上 / +Z = 前（基準の向きから見て）");

    // ---------------------------------------------------------
    // 4. 被写界深度
    // ---------------------------------------------------------
    changed |= DrawShotDofFields(context, shot);
    return changed;
}



bool CinematicEditorWindow::DrawShotDofFields(EditorContext &context, CameraShot &shot)
{
    ImGui::SeparatorText("被写界深度");

    bool changed = ImGui::Checkbox("ぼかす", &shot.dof);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("ピントは注視点に合う。開始と終了で注視点を変えるとピント送りになる");
    if (!shot.dof) return changed;

    changed |= ImGui::DragFloat("ぼけない幅", &shot.dofRange, 0.05f, 0.0f, 100.0f, "%.2f m");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("ピントの前後この距離までは、くっきり映す");
    changed |= ImGui::DragFloat("ぼけきる距離", &shot.dofFalloff, 0.1f, 0.1f, 200.0f, "%.1f m");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("短いほど急にぼける");
    changed |= ImGui::DragFloat("ぼけの強さ", &shot.dofMaxBlur, 0.0005f, 0.0f, 0.05f, "%.4f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("0.01 前後が目安");

    const bool hasDof = context.world && context.world->GetSingletonComponent<DepthOfFieldConfigComponent>();
    if (!hasDof) ImGui::TextColored(kWarnColor, "シーンに DepthOfField がないので、ぼけません");

    return changed;
}



bool CinematicEditorWindow::DrawSoundFields(EditorContext &context, CinematicSound &sound)
{
    bool changed = DragSeconds("時刻", sound.time);

    // アニメのシーケンサーの Sound と同じく、AudioConfig に登録した名前から選ぶ
    std::vector<std::string> names;
    if (context.world && context.world->HasResource<std::shared_ptr<IAudioAPI>>())
    {
        if (auto &api = context.world->GetResource<std::shared_ptr<IAudioAPI>>()) names = api->GetEventNames();
    }

    if (ImGui::BeginCombo("音", sound.event.empty() ? "（未設定）" : sound.event.c_str()))
    {
        if (names.empty()) ImGui::TextDisabled("AudioConfig に登録がありません");
        for (const std::string &n : names)
        {
            const bool selected = (n == sound.event);
            if (ImGui::Selectable(n.c_str(), selected))
            {
                sound.event = n;
                changed     = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    changed |= DrawSubjectField(context, sound.subject, "2D（距離で減衰しない）");
    changed |= ImGui::DragFloat("音量", &sound.volume, 0.01f, 0.0f, 4.0f, "%.2f 倍");
    changed |= ImGui::DragFloat("ピッチ", &sound.pitch, 0.01f, -1.0f, 1.0f, "%.2f オクターブ");
    return changed;
}



bool CinematicEditorWindow::DrawEffectFields(EditorContext &context, CinematicEffect &effect)
{
    bool changed = DragSeconds("時刻", effect.time);
    changed |= PathField("VFX プレハブ", effect.effect, "Prefab", true);
    changed |= DrawSubjectField(context, effect.subject, nullptr);
    changed |= DragVec3("ずれ", effect.offset);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("基準のローカル座標でのずれ（m）");
    changed |= ImGui::DragFloat("大きさ", &effect.scale, 0.01f, 0.0f, FLT_MAX, "%.2f 倍");
    changed |= ImGui::Checkbox("基準に付いていく", &effect.follow);
    changed |= DragSeconds("継続放出の時間", effect.duration);
    return changed;
}



bool CinematicEditorWindow::DrawSignalFields(CinematicSignal &signal)
{
    bool changed = DragSeconds("時刻", signal.time);
    changed |= InputString("名前", signal.name);
    changed |= InputString("引数1", signal.param);
    changed |= InputString("引数2", signal.param2);
    changed |= ImGui::DragFloat("引数3（数値）", &signal.value, 0.01f);
    ImGui::TextDisabled("Telop : 引数1 = 名前 / 引数2 = 下の行 / 引数3 = 出す秒数（0 なら 3 秒）");
    return changed;
}



bool CinematicEditorWindow::DrawAnimationFields(EditorContext &context, CinematicAnimation &anim)
{
    bool changed = DragSeconds("開始", anim.start);
    changed |= DragSeconds("長さ", anim.duration);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 ならクリップ1回分");
    ImGui::TextDisabled("実際の長さ %.2f 秒", anim.Length());

    changed |= DrawSubjectField(context, anim.subject, nullptr);
    if (PathField("クリップ", anim.sequence, "AnimSequence", false))
    {
        ResolveAnimation(anim);
        changed = true;
    }
    if (!anim.sequence.empty() && !anim.resolved) ImGui::TextColored(kWarnColor, "このアニメを読み込めません");

    changed |= ImGui::Checkbox("ループ", &anim.loop);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("ループするときは長さを書く");
    changed |= ImGui::DragFloat("速さ", &anim.speed, 0.01f, 0.01f, 10.0f, "%.2f 倍");
    changed |= DragSeconds("握るときのブレンド", anim.blendIn);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("同じ相手のアニメに続けるときは、前のアニメに少し重ねて置く");
    changed |= DragSeconds("離すときのブレンド", anim.blendOut);
    return changed;
}



bool CinematicEditorWindow::DrawSubjectField(EditorContext &context, std::string &subject, const char *emptyMeans)
{
    const bool changed = InputString("基準の名前", subject);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("シーンの Entity の名前。例: Player / Valkyra");

    if (subject.empty())
    {
        if (emptyMeans)
        {
            ImGui::TextDisabled("空 = %s", emptyMeans);
        }
        else
        {
            ImGui::TextColored(kWarnColor, "空にはできません");
        }
    }
    else if (context.world && Cinematic::FindEntityByName(*context.world, subject) == InvalidEntityID)
    {
        ImGui::TextColored(kWarnColor, "シーンに '%s' がいません", subject.c_str());
    }
    return changed;
}



// ---------------------------------------------------------------------------
// 再生とプレビュー
// ---------------------------------------------------------------------------

void CinematicEditorWindow::StartPlayback(EditorContext &context, float from)
{
    if (!context.world) return;
    auto &bus = context.world->GetEventBus();

    // 再生中に届いた再生要求は捨てられるので、先に止める。ゲーム側が流しているムービーも止まる
    auto script = std::make_shared<CinematicScript>(MakePlayableCopy());
    bus.Publish(CinematicStopRequestEvent{});
    bus.Publish(CinematicPlayRequestEvent{script->path, from, script});

    // ループで流し直すときは、最初に流す前の状態を覚えたままにする
    if (!m_playing) m_pausedBeforePlay = context.isPaused;
    context.isPaused = false;

    m_playing        = true;
    m_playStarted    = false;
    m_playWaitFrames = 0;
    m_playFrom       = from;
    m_time           = from;
}



void CinematicEditorWindow::StopPlayback(EditorContext &context, bool sendStop)
{
    if (sendStop && context.world) context.world->GetEventBus().Publish(CinematicStopRequestEvent{});

    // 止め直せば、流し終えたところの絵とエフェクトをそのまま見られる
    if (m_playing && m_pausedBeforePlay) context.isPaused = true;
    m_playing = false;
}



void CinematicEditorWindow::UpdatePlayback(EditorContext &context)
{
    if (!m_playing) return;

    // 要求は描画の段で受け付けられるので、出したフレームにはまだ流れていない
    if (m_followingGame)
    {
        m_playStarted = true;
        return;
    }

    // ムービー用カメラがないなどで受け付けられなければ、あきらめて元へ戻す
    if (!m_playStarted)
    {
        if (++m_playWaitFrames < kPlayStartTimeoutFrames) return;
        m_status = "流せませんでした（シーンにムービー用カメラがない、など。ログを確認）";
        StopPlayback(context, false);
        return;
    }

    // 流し終えた。ループなら同じ位置から流し直し、違えば流す前の一時停止へ戻す
    if (m_playLoop)
    {
        StartPlayback(context, m_playFrom);
        return;
    }
    StopPlayback(context, false);
}



bool CinematicEditorWindow::FollowGamePlayback(EditorContext &context)
{
    if (!context.world) return false;

    const std::string name      = PlaybackName();
    bool              following = false;
    context.world->ViewEach<CinematicCameraComponent>([&](EntityID id) {
        const auto *cine = context.world->GetComponent<CinematicCameraComponent>(id);
        if (!cine || cine->state != CinematicState::Playing || cine->script.path != name) return;

        m_time    = cine->time;
        following = true;
    });
    return following;
}



void CinematicEditorWindow::PushPreview(EditorContext &context, bool active)
{
    if (!context.world) return;
    World &world = *context.world;

    world.ViewEach<CinematicCameraComponent>([&](EntityID id) {
        auto *cine = world.GetComponent<CinematicCameraComponent>(id);
        if (!cine) return;

        cine->previewActive = active;
        if (!active) return;

        cine->previewScript = MakePlayableCopy();
        cine->previewTime   = m_time;
    });
}



CinematicScript CinematicEditorWindow::MakePlayableCopy() const
{
    CinematicScript copy = m_script;
    copy.path            = PlaybackName();

    // ドラッグや追加で崩れた並びを、再生側が前提にする時刻の順へ直す
    CinematicScriptIO::SortByTime(copy);

    auto &anims = copy.animations;
    anims.erase(std::remove_if(anims.begin(), anims.end(), [](const CinematicAnimation &a) { return !a.resolved; }),
                anims.end());
    return copy;
}



std::string CinematicEditorWindow::PlaybackName() const
{
    if (m_script.path.empty()) return kUnsavedPlaybackName;
    return m_script.path;
}



// ---------------------------------------------------------------------------
// 操作
// ---------------------------------------------------------------------------

bool CinematicEditorWindow::CaptureView(EditorContext &context, CameraShot &shot, bool toEnd)
{
    // 処理フロー:
    // 1. 画面に映っているカメラの位置・向き・画角を取る
    // 2. 基準の今の位置と向きを取る
    // 3. 注視点を決める
    // 4. 基準から見たずれに直して、開始か終了へ書く

    // ---------------------------------------------------------
    // 1. 画面のカメラ
    // ---------------------------------------------------------
    if (!context.world || !context.world->HasResource<Camera *>() || !context.world->GetResource<Camera *>())
    {
        m_status = "画面のカメラがないので取り込めません";
        return false;
    }
    const Camera &camera = *context.world->GetResource<Camera *>();

    const Vector3 eye = camera.GetEye();
    Vector3       front(camera.GetFront());
    front.Normalize();

    // ---------------------------------------------------------
    // 2. 基準
    // ---------------------------------------------------------
    Vector3 subjectPos;
    float   yaw = 0.0f;
    if (!Cinematic::FindSubjectPose(*context.world, shot.subject, subjectPos, yaw))
    {
        m_status = "基準 '" + shot.subject + "' がシーンにいないので取り込めません";
        return false;
    }

    // ---------------------------------------------------------
    // 3. 注視点
    // ---------------------------------------------------------

    // 画面のカメラは向きしか持たないので、視線の上で基準に一番近い点を注視点にする
    // 基準がカメラの後ろか近すぎるときは、5m 先にする
    float distance = (subjectPos - eye).Dot(front);
    if (distance < 1.0f) distance = 5.0f;
    const Vector3 look = eye + front * distance;

    // ---------------------------------------------------------
    // 4. 基準から見たずれ
    // ---------------------------------------------------------

    // 再生側は「ずれを基準の向きで回して足す」ので、逆に「引いてから逆向きに回す」
    const Matrix toLocal = Matrix::CreateRotationY(-yaw);
    (toEnd ? shot.lookEnd : shot.lookStart) = Vector3::TransformNormal(look - subjectPos, toLocal);

    if (shot.motion == CameraMotion::Orbit)
    {
        const Vector3 o     = Vector3::TransformNormal(eye - look, toLocal);
        float         angle = DirectX::XMConvertToDegrees(std::atan2(o.x, o.z));
        const float   other = toEnd ? shot.angleStart : shot.angleEnd;

        // 角度は数値のまま補間されるので、反対側の端に近い表し方にする（-170 と 170 で1周回らないように）
        while (angle - other > 180.0f) angle -= 360.0f;
        while (angle - other < -180.0f) angle += 360.0f;

        (toEnd ? shot.angleEnd : shot.angleStart)   = angle;
        (toEnd ? shot.radiusEnd : shot.radiusStart) = std::sqrt(o.x * o.x + o.z * o.z);
        (toEnd ? shot.heightEnd : shot.heightStart) = o.y;
    }
    else
    {
        (toEnd ? shot.eyeEnd : shot.eyeStart) = Vector3::TransformNormal(eye - subjectPos, toLocal);
    }

    // 正射影のカメラは画角が 0 なので書かない
    const float fovDeg = DirectX::XMConvertToDegrees(camera.GetFovY());
    if (fovDeg > 0.0f) (toEnd ? shot.fovEnd : shot.fovStart) = fovDeg;

    m_status = toEnd ? "今の画面を終了へ取り込みました" : "今の画面を開始へ取り込みました";
    return true;
}



void CinematicEditorWindow::Open(const std::string &path, bool keepView)
{
    CinematicScript loaded;
    if (!CinematicScriptIO::Load(path, loaded))
    {
        m_status = "読み込めませんでした（ログを確認）: " + path;
        return;
    }

    m_script = std::move(loaded);
    m_dirty  = false;
    m_status = "開きました : " + path;

    // 読み込みで時刻の順に並び直るので、選んでいた行は別の項目を指しうる
    m_selectedRow = -1;

    if (keepView)
    {
        m_time = std::min(m_time, m_script.duration);
    }
    else
    {
        m_time       = 0.0f;
        m_firstFrame = 0;
    }
}



void CinematicEditorWindow::NewScript()
{
    m_script      = CinematicScript{};
    m_dirty       = false;
    m_status      = "新しい台本です。保存すると保存先を選びます";
    m_time        = 0.0f;
    m_selectedRow = -1;
    m_firstFrame  = 0;
}



void CinematicEditorWindow::SaveAs(EditorContext &context)
{
    auto picked = FileDialog::SavePreset("Cinematic");
    if (!picked) return;

    // ゲームが読むのは台本フォルダの直下だけ
    const std::string path = FileDialog::ConvertToRelativePath(*picked);
    const std::string dir  = std::string(kCinematicScriptDir) + "/";
    if (path.rfind(dir, 0) != 0 || path.find('/', dir.size()) != std::string::npos)
    {
        m_status = "台本は " + std::string(kCinematicScriptDir) + " の直下に保存してください: " + path;
        return;
    }

    // 失敗したら元の保存先へ戻す。残すと、次の「保存」が失敗した先へ書きにいく
    const std::string previous = m_script.path;
    m_script.path              = path;
    if (!Save(context)) m_script.path = previous;
}



bool CinematicEditorWindow::Save(EditorContext &context)
{
    // まだ保存先がなければ、選ばせてから保存する
    if (m_script.path.empty())
    {
        SaveAs(context);
        return !m_script.path.empty();
    }

    // 台本を JSON へ書き出す
    if (!CinematicScriptIO::Save(m_script.path, m_script))
    {
        m_status = "保存できませんでした（ログを確認）: " + m_script.path;
        return false;
    }

    // 読み直して、ゲームが読むのと同じ形（時刻の順・アニメの読み込み）にそろえる
    const std::string path = m_script.path;
    Open(path, true);
    m_status = "保存しました : " + path;

    // ゲーム側にも台本を読み直させる
    if (context.world) context.world->GetEventBus().Publish(CinematicScriptsChangedEvent{});
    return true;
}

#else // ENABLE_EDITOR

// Release ではつなぎの実体（CinematicTimelineSequence.cpp）をビルドしないので、
// unique_ptr が要求する「完全な型」だけをここで用意する
class CinematicTimelineSequence
{
};

CinematicEditorWindow::CinematicEditorWindow() : EditorWindow("Cinematic Editor", false) {}
CinematicEditorWindow::~CinematicEditorWindow() = default;

void CinematicEditorWindow::DrawContents(EditorContext &) {}
void CinematicEditorWindow::OnFrameBegin(EditorContext &) {}
void CinematicEditorWindow::DrawToolbar(EditorContext &) {}
void CinematicEditorWindow::DrawTransportBar(EditorContext &) {}
void CinematicEditorWindow::DrawStatusLine(EditorContext &) {}
bool CinematicEditorWindow::DrawAddButtons() { return false; }
bool CinematicEditorWindow::DrawTimeline() { return false; }
bool CinematicEditorWindow::DrawInspector(EditorContext &) { return false; }
void CinematicEditorWindow::EraseSelected() {}
bool CinematicEditorWindow::DrawShotFields(EditorContext &, std::size_t) { return false; }
bool CinematicEditorWindow::DrawShotDofFields(EditorContext &, CameraShot &) { return false; }
bool CinematicEditorWindow::DrawSoundFields(EditorContext &, CinematicSound &) { return false; }
bool CinematicEditorWindow::DrawEffectFields(EditorContext &, CinematicEffect &) { return false; }
bool CinematicEditorWindow::DrawSignalFields(CinematicSignal &) { return false; }
bool CinematicEditorWindow::DrawAnimationFields(EditorContext &, CinematicAnimation &) { return false; }
bool CinematicEditorWindow::DrawSubjectField(EditorContext &, std::string &, const char *) { return false; }
void CinematicEditorWindow::StartPlayback(EditorContext &, float) {}
void CinematicEditorWindow::StopPlayback(EditorContext &, bool) {}
void CinematicEditorWindow::UpdatePlayback(EditorContext &) {}
bool CinematicEditorWindow::FollowGamePlayback(EditorContext &) { return false; }
void CinematicEditorWindow::PushPreview(EditorContext &, bool) {}
CinematicScript CinematicEditorWindow::MakePlayableCopy() const { return {}; }
std::string CinematicEditorWindow::PlaybackName() const { return {}; }
bool CinematicEditorWindow::CaptureView(EditorContext &, CameraShot &, bool) { return false; }
void CinematicEditorWindow::Open(const std::string &, bool) {}
void CinematicEditorWindow::NewScript() {}
bool CinematicEditorWindow::Save(EditorContext &) { return false; }
void CinematicEditorWindow::SaveAs(EditorContext &) {}

#endif // ENABLE_EDITOR
