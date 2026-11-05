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
    // 注意を引く文字の色。「未保存」「基準が見つからない」などで共通
    constexpr ImVec4 kWarnColor(1.0f, 0.75f, 0.2f, 1.0f);

    // --- 配置 ---
    constexpr float kInspectorWidth       = 400.0f; // 右のペインの幅
    constexpr float kLabelChars           = 9.0f;   // インスペクタで欄の右に残す名前の幅（文字数）。「握るときのブレンド」が収まる
    constexpr float kGroupSpacing         = 24.0f;  // 上の帯で、まとまりの間に空ける幅
    constexpr float kTransportButtonWidth = 56.0f;  // 再生・停止のボタンの幅（文字が替わっても幅を変えない）
    constexpr float kDurationFieldWidth   = 90.0f;  // 全体の長さの欄の幅
    constexpr float kPreviewModeWidth     = 150.0f; // 画面への出し方の欄の幅

    // 画面への出し方の名前（PreviewMode の並びと同じ順）
    constexpr const char *kPreviewModeNames[] = {"出さない", "一時停止中だけ", "止めずに出す"};

    // --- 欄の部品 ---

    // 文字列の欄。変えたら true
    bool InputString(const char *label, std::string &value)
    {
        char buf[256];
        strcpy_s(buf, sizeof(buf), value.c_str());
        if (!ImGui::InputText(label, buf, sizeof(buf))) return false;
        value = buf;
        return true;
    }



    // 秒の欄。変えたら true
    bool DragSeconds(const char *label, float &value, float min = 0.0f)
    {
        return ImGui::DragFloat(label, &value, 0.01f, min, FLT_MAX, "%.2f 秒");
    }



    // 3つの数値（位置・ずれ）の欄。変えたら true
    bool DragVec3(const char *label, DirectX::XMFLOAT3 &v)
    {
        return ImGui::DragFloat3(label, &v.x, 0.05f, 0.0f, 0.0f, "%.2f");
    }



    /**
     * @brief 列挙値を名前の一覧から選ぶ欄
     * @tparam Enum 列挙型
     * @tparam N    名前の数
     * @param label 欄の名前
     * @param names 列挙値の名前（列挙の並びと同じ順）
     * @param value 選んでいる値
     * @return 変えたら true
     */
    template <class Enum, std::size_t N>
    bool ComboEnum(const char *label, const char *const (&names)[N], Enum &value)
    {
        int index = static_cast<int>(value);
        if (!ImGui::Combo(label, &index, names, static_cast<int>(N))) return false;
        value = static_cast<Enum>(index);
        return true;
    }



    /**
     * @brief ファイルのパスの欄。手で打たせず、参照ボタンの FileDialog で選ぶ
     * @param label    欄の名前
     * @param path     パス
     * @param preset   FileDialog のプリセット
     * @param stripExt 拡張子を落として書くか（エフェクトのプレハブは拡張子なしで指す）
     * @return 選び直したら true
     */
    bool PathField(const char *label, std::string &path, const char *preset, bool stripExt)
    {
        // パスは手で打たせない（綴りを1文字間違えると読めず、原因に気づきにくい）
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



    // --- 台本 ---

    /**
     * @brief 時刻 t で映るショットを返す（再生と同じ「開始時刻が t 以下のうち最後」）
     *
     * 編集中の写しは並び替えていないので、並び順に頼らず開始時刻で比べる
     *
     * @return 映るショット。ショットがなければ nullptr
     */
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



    // アニメのファイルを読み、長さの自動（0）に使うクリップの長さを入れる
    void ResolveAnimation(CinematicAnimation &anim)
    {
        anim.resolved     = anim.sequence.empty() ? nullptr : ResourceManager::Instance().LoadAnimSequence(anim.sequence.c_str());
        anim.clipDuration = anim.resolved ? anim.resolved->duration : 0.0f;
    }
} // namespace

// ---------------------------------------------------------------------------
// 生成・破棄
// ---------------------------------------------------------------------------

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
    // 処理フロー:
    // 1. 上の帯: ファイルの操作、再生の操作、直近の結果と注意
    // 2. 左のペイン: 項目を足すボタンと、タイムライン
    // 3. 右のペイン: 選んでいる項目の数値（何も選んでいなければ台本の設定）

    // 項目を足すのはタイムラインより先、数値と消すボタンはタイムラインより後に描く
    // タイムラインが行と台本の項目の対応を作り直すので、足した項目を同じフレームで並べ、数値の欄はその対応で引くため

    // ---------------------------------------------------------
    // 1. 上の帯
    // ---------------------------------------------------------
    DrawToolbar(context);
    ImGui::Separator();

    DrawTransportBar(context);
    DrawStatusLine(context);

    // ---------------------------------------------------------
    // 2. 左のペイン
    // ---------------------------------------------------------
    const float timelineWidth = -(kInspectorWidth + ImGui::GetStyle().ItemSpacing.x);

    ImGui::BeginChild("timelinePane", ImVec2(timelineWidth, 0.0f), true);
    m_dirty |= DrawAddButtons();
    m_dirty |= DrawTimeline();
    ImGui::EndChild();

    // ---------------------------------------------------------
    // 3. 右のペイン
    // ---------------------------------------------------------
    ImGui::SameLine();
    ImGui::BeginChild("inspectorPane", ImVec2(0.0f, 0.0f), true);
    m_dirty |= DrawInspector(context);
    ImGui::EndChild();
}



void CinematicEditorWindow::OnFrameBegin(EditorContext &context)
{
    // 処理フロー:
    // 1. ゲームでこの台本を流していれば、再生位置をムービーに付いていかせる
    // 2. エディタ内の再生なら、再生位置を実時間で進める
    // 3. プレビューを出す・止める

    // ---------------------------------------------------------
    // 1. ゲームでの再生に付いていく
    // ---------------------------------------------------------
    m_followingGame = GetVisible() && FollowGamePlayback(context);

    // ---------------------------------------------------------
    // 2. エディタ内の再生
    // ---------------------------------------------------------
    const bool active = IsPreviewShown(context);

    // プレビューが出ていない・ゲームで流している（そちらの再生位置を見せる）間は、エディタ内の再生を止める
    if (!active || m_followingGame)
    {
        m_previewPlaying = false;
    }
    if (m_previewPlaying)
    {
        StepPreviewPlayback();
    }

    // ---------------------------------------------------------
    // 3. プレビュー
    // ---------------------------------------------------------

    // 止めるのは、出していたときの1回だけ
    if (active || m_previewPushed)
    {
        PushPreview(context, active);
    }
    m_previewPushed = active;
}



bool CinematicEditorWindow::IsPreviewShown(const EditorContext &context) const
{
    if (!GetVisible())
    {
        return false;
    }

    // 「止めずに出す」の間は、カメラ側がムービー中として扱い、プレイヤーの操作と敵の思考を止める
    switch (m_previewMode)
    {
    case PreviewMode::WhilePaused:
        return context.isPaused;
    case PreviewMode::Live:
        return true;
    case PreviewMode::Off:
    default:
        return false;
    }
}



bool CinematicEditorWindow::FollowGamePlayback(EditorContext &context)
{
    if (!context.world || m_script.path.empty()) return false;

    bool following = false;
    context.world->ViewEach<CinematicCameraComponent>([&](EntityID id) {
        const auto *cine = context.world->GetComponent<CinematicCameraComponent>(id);
        if (!cine) return;

        // 開いている台本を流しているときだけ合わせる。別の台本の再生位置は、この時間軸では意味がない
        const bool playing = (cine->state == CinematicState::Waiting) || (cine->state == CinematicState::Playing);
        if (!playing || cine->script.path != m_script.path) return;

        m_time    = cine->time;
        following = true;
    });
    return following;
}



void CinematicEditorWindow::StepPreviewPlayback()
{
    // 実時間で進める。一時停止中はゲームの時計（dt）が 0 なので、ゲームの時計には頼らない
    m_time += ImGui::GetIO().DeltaTime;
    if (m_time < m_script.duration) return;

    if (m_previewLoop && m_script.duration > 0.0f)
    {
        m_time = std::fmod(m_time, m_script.duration);
    }
    else
    {
        m_time           = m_script.duration;
        m_previewPlaying = false;
    }
}



void CinematicEditorWindow::DrawToolbar(EditorContext &context)
{
    // 処理フロー:
    // 1. 開く（新規・開く・開き直す）
    // 2. 保存（保存・名前を付けて保存）
    // 3. 開いている台本の名前と、未保存の印

    const bool hasFile = !m_script.path.empty(); // 一度でも保存した（ファイルがある）台本か

    // ---------------------------------------------------------
    // 1. 開く
    // ---------------------------------------------------------
    if (ImGui::Button("新規"))
    {
        NewScript();
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("空の台本から作る（保存していない変更は捨てる）");
    }

    ImGui::SameLine();
    if (ImGui::Button("開く..."))
    {
        if (auto picked = FileDialog::OpenPreset("Cinematic"))
        {
            Open(FileDialog::ConvertToRelativePath(*picked), false);
        }
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(!hasFile);
    if (ImGui::Button("開き直す"))
    {
        Open(m_script.path, true);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("いじった分を捨てて、保存した状態に戻す");
    }

    // ---------------------------------------------------------
    // 2. 保存
    // ---------------------------------------------------------
    ImGui::SameLine(0.0f, kGroupSpacing);
    if (ImGui::Button("保存"))
    {
        Save(context);
    }

    ImGui::SameLine();
    if (ImGui::Button("名前を付けて保存..."))
    {
        SaveAs(context);
    }

    // ---------------------------------------------------------
    // 3. 開いている台本
    // ---------------------------------------------------------
    ImGui::SameLine(0.0f, kGroupSpacing);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(hasFile ? m_script.path.c_str() : "（新しい台本）");

    if (m_dirty)
    {
        ImGui::SameLine();
        ImGui::TextColored(kWarnColor, "未保存");
    }
}



void CinematicEditorWindow::DrawTransportBar(EditorContext &context)
{
    // 処理フロー:
    // 1. エディタ内の再生（再生・頭へ・ループ）
    // 2. 再生位置と全体の長さ
    // 3. ゲーム画面への出し方
    // 4. ゲームで流す

    const bool hasFile = !m_script.path.empty();
    const bool shown   = IsPreviewShown(context);

    // ---------------------------------------------------------
    // 1. エディタ内の再生
    // ---------------------------------------------------------

    // 再生できるのは、ゲーム画面に絵を出している間だけ（出していなければ、進めても見えない）
    ImGui::BeginDisabled(!shown || m_followingGame);
    if (ImGui::Button(m_previewPlaying ? "停止" : "再生", ImVec2(kTransportButtonWidth, 0.0f)))
    {
        // 終わりで止まっていたら、頭から流し直す
        if (!m_previewPlaying && m_time >= m_script.duration)
        {
            m_time = 0.0f;
        }
        m_previewPlaying = !m_previewPlaying;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("カメラの動きだけを流して、構図とタイミングを見る（保存しなくてよい）");
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(m_followingGame);
    if (ImGui::Button("頭へ"))
    {
        m_time = 0.0f;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::Checkbox("ループ", &m_previewLoop);

    // ---------------------------------------------------------
    // 2. 再生位置と全体の長さ
    // ---------------------------------------------------------
    ImGui::SameLine(0.0f, kGroupSpacing);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%.2f /", m_time);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(kDurationFieldWidth);
    m_dirty |= DragSeconds("##duration", m_script.duration, 0.1f);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("ムービー全体の長さ");
    }

    // ---------------------------------------------------------
    // 3. ゲーム画面への出し方
    // ---------------------------------------------------------
    ImGui::SameLine(0.0f, kGroupSpacing);
    ImGui::TextUnformatted("画面:");

    ImGui::SameLine();
    ImGui::SetNextItemWidth(kPreviewModeWidth);
    ComboEnum("##previewMode", kPreviewModeNames, m_previewMode);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("一時停止中だけ : Tab で止めて、じっくり構図を詰める\n"
                          "止めずに出す : キャラのアニメを動かしたまま見る（操作と敵は止まる）");
    }

    // ---------------------------------------------------------
    // 4. ゲームで流す
    // ---------------------------------------------------------

    // ゲームと同じ再生要求で流す。読むのはゲーム側が持つ台本なので、保存してからでないと古い方が流れる
    ImGui::SameLine(0.0f, kGroupSpacing);
    ImGui::TextUnformatted("ゲームで流す:");

    ImGui::SameLine();
    ImGui::BeginDisabled(m_dirty || !hasFile);
    if (ImGui::Button("頭から") && context.world)
    {
        context.world->GetEventBus().Publish(CinematicPlayRequestEvent{m_script.path});
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("SE・エフェクト・アニメまで本番どおりに確かめる\n保存してから、一時停止を解いて押す");
    }

    // 途中から流す。イベントの少し手前に再生位置を置いて押せば、そこだけを何度でも見直せる
    ImGui::SameLine();
    if (ImGui::Button("ここから") && context.world)
    {
        context.world->GetEventBus().Publish(CinematicPlayRequestEvent{m_script.path, m_time});
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("再生位置（%.2f 秒）から流す。気になる所だけ見直すときに", m_time);
    }
}



void CinematicEditorWindow::DrawStatusLine(EditorContext &context)
{
    // 処理フロー:
    // 1. 今の状態の注意（ゲームで再生中・基準がいない・一時停止待ち）
    // 2. 注意がなければ、直近の読み込み・保存・取り込みの結果

    // いつも1行だけ描く。行が増えたり消えたりすると、下のタイムラインが上下に跳ねるため

    // ---------------------------------------------------------
    // 1. 今の状態の注意
    // ---------------------------------------------------------
    if (m_followingGame)
    {
        ImGui::TextColored(kWarnColor, "ゲームで再生中（再生位置が付いていきます）");
        return;
    }

    if (IsPreviewShown(context))
    {
        // 基準がいないショットは絵を作れない。カメラ側は毎フレーム警告を出せないので、ここで知らせる
        const CameraShot *shot = ShotAt(m_script.shots, m_time);
        if (shot && !shot->subject.empty() && context.world &&
            Cinematic::FindEntityByName(*context.world, shot->subject) == InvalidEntityID)
        {
            ImGui::TextColored(kWarnColor, "ショットの基準 '%s' がシーンに居ないので、画面に出せません",
                               shot->subject.c_str());
            return;
        }
    }
    else if (m_previewMode == PreviewMode::WhilePaused)
    {
        ImGui::TextDisabled("Tab で一時停止すると、ゲーム画面に絵が出ます");
        return;
    }

    // ---------------------------------------------------------
    // 2. 直近の結果
    // ---------------------------------------------------------
    ImGui::TextDisabled("%s", m_status.c_str());
}



bool CinematicEditorWindow::DrawAddButtons()
{
    // 処理フロー:
    // 1. 置く時刻を決める
    // 2. 種類ごとのボタン。押したら足して、その項目を選ぶ
    // 3. タイムラインの操作の早見

    // ---------------------------------------------------------
    // 1. 置く時刻
    // ---------------------------------------------------------

    // 足すときは再生位置に置く。全体の長さちょうどには置けない（読み込みが「映らない」と弾く）ので、1コマ手前までにする
    const float lastTime = std::max(0.0f, m_script.duration - 1.0f / CinematicTimelineSequence::kFramesPerSecond);
    const float at       = std::clamp(m_time, 0.0f, lastTime);

    bool        added = false;
    RowKind     kind  = RowKind::Shot;
    std::size_t index = 0;

    // ---------------------------------------------------------
    // 2. 種類ごとのボタン
    // ---------------------------------------------------------
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("足す:");
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("再生位置に置く");
    }

    ImGui::SameLine();
    if (ImGui::Button("ショット"))
    {
        // 再生位置で映っているショットを写して足す。一から数値を書くより、近い構図から直す方が早い
        CameraShot shot;
        if (const CameraShot *base = ShotAt(m_script.shots, at))
        {
            shot = *base;
        }
        shot.start    = at;
        shot.duration = 1.0f;
        shot.blendIn  = 0.0f;
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
    if (added)
    {
        m_selectedRow = CinematicTimelineSequence::RowOf(m_script, kind, index);
    }

    // ---------------------------------------------------------
    // 3. 操作の早見（右端に寄せる）
    // ---------------------------------------------------------
    const char *helpLabel = "(?)";
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                         std::max(0.0f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(helpLabel).x));
    ImGui::TextDisabled("%s", helpLabel);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("帯の中をドラッグ : 時刻をずらす\n"
                          "帯の端をドラッグ : 長さを変える\n"
                          "上の目盛りをクリック : 再生位置を動かす\n"
                          "Ctrl + ホイール : 拡大・縮小");
    }
    return added;
}



bool CinematicEditorWindow::DrawTimeline()
{
    int       frame  = CinematicTimelineSequence::TimeToFrame(m_time);
    const int before = frame;
    m_sequence->Begin(m_script, frame);

    // ImSequencer は項目が1つもないと何も描かない
    if (m_sequence->GetItemCount() == 0)
    {
        ImGui::Spacing();
        ImGui::TextDisabled("「足す」からショットを置くと、ここに帯が並びます");
        return false;
    }

    // ImSequencer は残りの高さを全部使うので、行数ぶんの高さの子ウィンドウに閉じ込める
    //   目盛り(20) + 行(20 x 数) + 下の横スクロール(14) + 内側の余白(8) + 少しの余裕
    constexpr float kItemHeight = 20.0f;
    const float     height      = kItemHeight * (m_sequence->GetItemCount() + 1) + 14.0f + 8.0f + 4.0f;

    ImGui::BeginChild("timeline", ImVec2(0.0f, height));
    ImSequencer::Sequencer(m_sequence.get(), &frame, nullptr, &m_selectedRow, &m_firstFrame,
                           ImSequencer::SEQUENCER_EDIT_STARTEND | ImSequencer::SEQUENCER_CHANGE_FRAME);
    ImGui::EndChild();

    if (frame != before)
    {
        m_time = CinematicTimelineSequence::FrameToTime(frame);
    }

    return m_sequence->Flush();
}



bool CinematicEditorWindow::DrawInspector(EditorContext &context)
{
    // 処理フロー:
    // 1. 何も選んでいなければ、台本の設定を出す
    // 2. 選んでいる項目の名前と、消すボタン
    // 3. 種類ごとの数値の欄

    // 欄の幅は、右に名前が収まるだけ残して、ペインの幅いっぱいにする
    const float fieldWidth = -ImGui::GetFontSize() * kLabelChars;

    // ---------------------------------------------------------
    // 1. 台本の設定
    // ---------------------------------------------------------
    CinematicTimelineSequence::Row row;
    if (!m_sequence->RowAt(m_selectedRow, row))
    {
        ImGui::SeparatorText("台本の設定");
        ImGui::PushItemWidth(fieldWidth);
        const bool changed = DrawScriptSettings();
        ImGui::PopItemWidth();

        ImGui::Spacing();
        ImGui::TextDisabled("タイムラインの帯を選ぶと、その項目を直せます");
        return changed;
    }

    // ---------------------------------------------------------
    // 2. 名前と消すボタン
    // ---------------------------------------------------------
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

    // ---------------------------------------------------------
    // 3. 数値の欄
    // ---------------------------------------------------------
    bool changed = false;
    ImGui::PushItemWidth(fieldWidth);

    // 行の対応は、このフレームのタイムラインで作り直したもの（項目の足し消しはその前に済んでいる）
    switch (row.kind)
    {
    case RowKind::Shot:
        changed = DrawShotFields(context, m_script.shots[row.index]);
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



bool CinematicEditorWindow::DrawScriptSettings()
{
    bool changed = false;

    changed |= DragSeconds("開始待ち", m_script.startDelay);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("最初の絵で止めて見せる時間。例: 登場前の溜め");
    }

    changed |= ImGui::Checkbox("長押しでスキップできる", &m_script.allowSkip);
    return changed;
}



void CinematicEditorWindow::EraseSelected()
{
    CinematicTimelineSequence::Row row;
    if (!m_sequence->RowAt(m_selectedRow, row))
    {
        return;
    }

    auto eraseAt = [](auto &items, std::size_t i) {
        if (i < items.size())
        {
            items.erase(items.begin() + static_cast<std::ptrdiff_t>(i));
        }
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

bool CinematicEditorWindow::DrawShotFields(EditorContext &context, CameraShot &shot)
{
    // 処理フロー:
    // 1. 時間（開始・長さ・入りのブレンド）
    // 2. 動き（基準・動かし方・速さのカーブ）
    // 3. 構図（開始と終了をタブで切り替える）
    // 4. 被写界深度

    bool changed = false;

    // ---------------------------------------------------------
    // 1. 時間
    // ---------------------------------------------------------
    ImGui::SeparatorText("時間");
    changed |= DragSeconds("開始", shot.start);
    changed |= DragSeconds("長さ", shot.duration, 0.01f);
    changed |= DragSeconds("入りのブレンド", shot.blendIn);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("0 でカット。前のショットから滑らかにつなぐときに");
    }

    // ---------------------------------------------------------
    // 2. 動き
    // ---------------------------------------------------------
    ImGui::SeparatorText("動き");
    changed |= DrawSubjectField(context, shot.subject, "ワールドの原点");
    changed |= ComboEnum("動かし方", kCameraMotionNames, shot.motion);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Truck : 開始から終了へまっすぐ動く\nOrbit : 注視点の周りを回り込む");
    }
    changed |= ComboEnum("速さのカーブ", kCameraEaseNames, shot.ease);

    // ---------------------------------------------------------
    // 3. 構図
    // ---------------------------------------------------------

    // 開始と終了は同じ並びなので、タブで切り替えて1つずつ見せる（縦に2つ並べると長くなりすぎる）
    ImGui::SeparatorText("構図");
    const bool orbit = (shot.motion == CameraMotion::Orbit);

    if (ImGui::BeginTabBar("framing"))
    {
        for (int side = 0; side < 2; ++side)
        {
            const bool toEnd = (side == 1);
            if (!ImGui::BeginTabItem(toEnd ? "終了" : "開始"))
            {
                continue;
            }

            if (ImGui::Button("今の画面を取り込む", ImVec2(-FLT_MIN, 0.0f)))
            {
                changed |= CaptureView(context, shot, toEnd);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("自由カメラで決めた構図を、そのまま入れる");
            }

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
    bool changed = false;
    ImGui::SeparatorText("被写界深度");

    changed |= ImGui::Checkbox("ぼかす", &shot.dof);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("ピントは注視点に合います（カメラから注視点までの距離）\n"
                          "開始と終了で注視点を変えると、ピント送りになります");

    if (!shot.dof) return changed;

    ImGui::SameLine();
    ImGui::TextDisabled("ピント %.1f m → %.1f m", FocusDistance(shot, false), FocusDistance(shot, true));

    changed |= ImGui::DragFloat("ぼけない幅", &shot.dofRange, 0.05f, 0.0f, 100.0f, "%.2f m");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("ピントの前後この距離までは、くっきり映す");
    changed |= ImGui::DragFloat("ぼけきる距離", &shot.dofFalloff, 0.1f, 0.1f, 200.0f, "%.1f m");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("ぼけない幅の外から、一番ぼけるまでの距離。短いほど急にぼける");
    changed |= ImGui::DragFloat("ぼけの強さ", &shot.dofMaxBlur, 0.0005f, 0.0f, 0.05f, "%.4f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("一番ぼけたときの半径（画面に対する割合）。0.01 前後が目安");

    // ぼかすのはシーンの DepthOfField。ないシーンでは何も起きないので、ここで知らせる
    if (context.world && !context.world->GetSingletonComponent<DepthOfFieldConfigComponent>())
        ImGui::TextColored(kWarnColor, "シーンに DepthOfField が無いので、ぼけません");

    return changed;
}



float CinematicEditorWindow::FocusDistance(const CameraShot &shot, bool toEnd)
{
    // 基準の向きで回しても距離は変わらないので、台本のずれのまま測る
    if (shot.motion == CameraMotion::Orbit)
    {
        const float radius = toEnd ? shot.radiusEnd : shot.radiusStart;
        const float height = toEnd ? shot.heightEnd : shot.heightStart;
        return std::sqrt(radius * radius + height * height);
    }

    const Vector3 eye  = toEnd ? shot.eyeEnd : shot.eyeStart;
    const Vector3 look = toEnd ? shot.lookEnd : shot.lookStart;
    return Vector3::Distance(eye, look);
}



bool CinematicEditorWindow::DrawSoundFields(EditorContext &context, CinematicSound &sound)
{
    bool changed = false;

    changed |= DragSeconds("時刻", sound.time);

    // 音は AudioConfig に登録した名前から選ぶ（アニメのシーケンサーの Sound と同じ出どころ）
    std::vector<std::string> names;
    if (context.world && context.world->HasResource<std::shared_ptr<IAudioAPI>>())
        if (auto &api = context.world->GetResource<std::shared_ptr<IAudioAPI>>()) names = api->GetEventNames();

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
    bool changed = false;

    changed |= DragSeconds("時刻", effect.time);
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
    bool changed = false;

    changed |= DragSeconds("時刻", signal.time);
    changed |= InputString("名前", signal.name);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("ゲーム側の受け手（CinematicSignalSystem）が名前で意味を決める\n例: Telop");
    changed |= InputString("引数1", signal.param);
    changed |= InputString("引数2", signal.param2);
    changed |= ImGui::DragFloat("引数3（数値）", &signal.value, 0.01f);
    ImGui::TextDisabled("Telop : 引数1 = 名前 / 引数2 = 下の行 / 引数3 = 出す秒数（0 なら 3 秒）");
    return changed;
}



bool CinematicEditorWindow::DrawAnimationFields(EditorContext &context, CinematicAnimation &anim)
{
    bool changed = false;

    changed |= DragSeconds("開始", anim.start);
    changed |= DragSeconds("長さ", anim.duration);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("0 ならクリップ1回分");
    }
    ImGui::TextDisabled("実際の長さ %.2f 秒", anim.Length());

    changed |= DrawSubjectField(context, anim.subject, nullptr);
    if (PathField("クリップ", anim.sequence, "AnimSequence", false))
    {
        ResolveAnimation(anim);
        changed = true;
    }
    if (!anim.sequence.empty() && !anim.resolved) ImGui::TextColored(kWarnColor, "このアニメを読み込めません");

    changed |= ImGui::Checkbox("ループ", &anim.loop);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("ループは終わりが無いので、長さを 0 にできない");
    changed |= ImGui::DragFloat("速さ", &anim.speed, 0.01f, 0.01f, 10.0f, "%.2f 倍");
    changed |= DragSeconds("握るときのブレンド", anim.blendIn);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("同じ相手のアニメに続けるときは、前のアニメに少し重ねて置く\n"
                          "重なった頭から、この時間で前のアニメから切り替わる");
    changed |= DragSeconds("離すときのブレンド", anim.blendOut);
    return changed;
}



bool CinematicEditorWindow::DrawSubjectField(EditorContext &context, std::string &subject, const char *emptyMeans)
{
    const bool changed = InputString("基準の名前", subject);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("シーンの Entity の名前（NameComponent）。例: Player / Valkyra");

    if (subject.empty())
    {
        if (emptyMeans)
            ImGui::TextDisabled("空 = %s", emptyMeans);
        else
            ImGui::TextColored(kWarnColor, "空にはできません");
    }
    else if (context.world && Cinematic::FindEntityByName(*context.world, subject) == InvalidEntityID)
    {
        ImGui::TextColored(kWarnColor, "シーンに '%s' が居ません", subject.c_str());
    }
    return changed;
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
        m_status = "画面のカメラが無いので取り込めません";
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
        m_status = "基準 '" + shot.subject + "' がシーンに居ないので取り込めません";
        return false;
    }

    // ---------------------------------------------------------
    // 3. 注視点
    // ---------------------------------------------------------

    // 画面のカメラは向きしか持たないので、視線の上のどこを注視点にするかを決める
    // 視線の上で基準に一番近い点にすると、Orbit の中心が基準の近くに来る
    // 基準がカメラの後ろ（または近すぎる）なら、5m 先にする
    float distance = (subjectPos - eye).Dot(front);
    if (distance < 1.0f) distance = 5.0f;
    const Vector3 look = eye + front * distance;

    // ---------------------------------------------------------
    // 4. 基準から見たずれ
    // ---------------------------------------------------------

    // 再生側は「ずれを基準の向きで回して足す」ので、逆に「引いてから逆向きに回す」
    const Matrix toLocal = Matrix::CreateRotationY(-yaw);

    const Vector3 lookOffset = Vector3::TransformNormal(look - subjectPos, toLocal);
    (toEnd ? shot.lookEnd : shot.lookStart) = lookOffset;

    if (shot.motion == CameraMotion::Orbit)
    {
        // Orbit は注視点からのずれを、角度（0=前, 90=右）・水平の距離・高さに直す
        const Vector3 o      = Vector3::TransformNormal(eye - look, toLocal);
        float         angle  = DirectX::XMConvertToDegrees(std::atan2(o.x, o.z));
        const float   other  = toEnd ? shot.angleStart : shot.angleEnd;

        // 角度は数値のまま補間されるので、反対側の端に一番近い表し方にする（-170 と 170 で1周近く回らないように）
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

    // 正射影のカメラには画角がない（0）ので、そのときは書かない
    const float fovDeg = DirectX::XMConvertToDegrees(camera.GetFovY());
    if (fovDeg > 0.0f) (toEnd ? shot.fovEnd : shot.fovStart) = fovDeg;

    m_status = toEnd ? "今の画面を終了へ取り込みました" : "今の画面を開始へ取り込みました";
    return true;
}



void CinematicEditorWindow::PushPreview(EditorContext &context, bool active)
{
    if (!context.world) return;
    World &world = *context.world;

    world.ViewEach<CinematicCameraComponent>([&](EntityID id) {
        auto *cine = world.GetComponent<CinematicCameraComponent>(id);
        if (!cine) return;

        cine->previewActive = active;

        // 一時停止せずに見ているときだけ、操作と敵の思考を止めてもらう（一時停止中はもう止まっている）
        cine->previewFreeze = active && !context.isPaused;
        if (!active) return;

        cine->previewScript = m_script;
        cine->previewTime   = m_time;

        // カメラ側のショットの探し方は開始時刻の昇順を前提にする
        // 編集中の写しはドラッグや追加で順が崩れているので、渡す写しだけ並べる（読み込みと同じく同時刻は書いた順）
        std::stable_sort(cine->previewScript.shots.begin(), cine->previewScript.shots.end(),
                         [](const CameraShot &a, const CameraShot &b) { return a.start < b.start; });
    });
}



void CinematicEditorWindow::Open(const std::string &path, bool keepView)
{
    CinematicScript loaded;
    if (!CinematicScriptIO::Load(path, loaded))
    {
        m_status = "読み込めませんでした（コンソールを確認）: " + path;
        return;
    }

    m_script = std::move(loaded);
    m_dirty  = false;
    m_status = "開きました : " + path;

    // 読み込みで時刻の順へ並び直るので、選んでいた行は別の項目を指しうる。選び直してもらう
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

    const std::string path = FileDialog::ConvertToRelativePath(*picked);

    // ゲームが読むのは台本フォルダの直下だけ。それ以外へ保存すると、「ゲームで再生」しても見つからない
    const std::string dir = std::string(kCinematicScriptDir) + "/";
    if (path.rfind(dir, 0) != 0 || path.find('/', dir.size()) != std::string::npos)
    {
        m_status = "台本は " + std::string(kCinematicScriptDir) + " の直下に保存してください（ゲームはそこだけを読む）: " + path;
        return;
    }

    // 保存に失敗したときは元の保存先へ戻す。新しい保存先を残すと、次の「保存」が失敗した先へ書きにいく
    const std::string previous = m_script.path;
    m_script.path              = path;
    if (!Save(context)) m_script.path = previous;
}



bool CinematicEditorWindow::Save(EditorContext &context)
{
    // まだ一度も保存していない台本は、保存先を選ばせる
    if (m_script.path.empty())
    {
        SaveAs(context);
        return !m_script.path.empty();
    }

    // 書けるかどうかは、読み込みと同じ決まりで CinematicScriptIO が確かめる
    std::string err;
    if (!CinematicScriptIO::Save(m_script.path, m_script, err))
    {
        m_status = "保存できませんでした : " + err;
        return false;
    }

    // 読み直して、ゲームが読むのと同じ形（時刻の順・アニメの読み込み）にそろえる
    const std::string path = m_script.path;
    Open(path, true);
    m_status = "保存しました : " + path;

    // ゲーム側が持つ台本の一覧も読み直させる。保存してすぐ「ゲームで再生」を押したときに、新しい台本で流すため
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



bool CinematicEditorWindow::DrawScriptSettings() { return false; }



bool CinematicEditorWindow::DrawTimeline() { return false; }



bool CinematicEditorWindow::DrawAddButtons() { return false; }



bool CinematicEditorWindow::DrawInspector(EditorContext &) { return false; }



void CinematicEditorWindow::EraseSelected() {}



bool CinematicEditorWindow::IsPreviewShown(const EditorContext &) const { return false; }



bool CinematicEditorWindow::DrawShotFields(EditorContext &, CameraShot &) { return false; }



bool CinematicEditorWindow::DrawShotDofFields(EditorContext &, CameraShot &) { return false; }



float CinematicEditorWindow::FocusDistance(const CameraShot &, bool) { return 0.0f; }



bool CinematicEditorWindow::FollowGamePlayback(EditorContext &) { return false; }



void CinematicEditorWindow::StepPreviewPlayback() {}



bool CinematicEditorWindow::DrawSoundFields(EditorContext &, CinematicSound &) { return false; }



bool CinematicEditorWindow::DrawEffectFields(EditorContext &, CinematicEffect &) { return false; }



bool CinematicEditorWindow::DrawSignalFields(CinematicSignal &) { return false; }



bool CinematicEditorWindow::DrawAnimationFields(EditorContext &, CinematicAnimation &) { return false; }



bool CinematicEditorWindow::DrawSubjectField(EditorContext &, std::string &, const char *) { return false; }



bool CinematicEditorWindow::CaptureView(EditorContext &, CameraShot &, bool) { return false; }



void CinematicEditorWindow::PushPreview(EditorContext &, bool) {}



void CinematicEditorWindow::Open(const std::string &, bool) {}



void CinematicEditorWindow::NewScript() {}



bool CinematicEditorWindow::Save(EditorContext &) { return false; }



void CinematicEditorWindow::SaveAs(EditorContext &) {}

#endif // ENABLE_EDITOR
