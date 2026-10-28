#include "Editor/Windows/Cinematic/CinematicEditorWindow.h"

#include "Engine/Platform/Core/Dialog.h"

#ifdef ENABLE_EDITOR
#include "Engine/Animation/Core/AnimSequence.h"
#include "Engine/Audio/IAudioAPI.h"
#include "Engine/Cinematic/CinematicEvents.h"
#include "Engine/Cinematic/CinematicSubject.h"
#include "Engine/Cinematic/Components/CinematicCameraComponent.h"
#include "Engine/ECS/Core/CCL_World.h"
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
    // 注意を引く文字の色。「未保存」「基準が見つからない」などで共通。
    constexpr ImVec4 kWarnColor(1.0f, 0.75f, 0.2f, 1.0f);

    // 数値の欄の幅
    constexpr float kFieldWidth = 220.0f;

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
     * @brief 列挙値を名前の一覧から選ぶ欄。
     * @tparam Enum 列挙型。
     * @tparam N    名前の数。
     * @param label 欄の名前。
     * @param names 列挙値の名前（列挙の並びと同じ順）。
     * @param value 選んでいる値。
     * @return 変えたら true。
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
     * @brief ファイルのパスの欄。手で打たせず、参照ボタンの FileDialog で選ぶ。
     * @param label    欄の名前。
     * @param path     パス。
     * @param preset   FileDialog のプリセット。
     * @param stripExt 拡張子を落として書くか（エフェクトのプレハブは拡張子なしで指す）。
     * @return 選び直したら true。
     */
    bool PathField(const char *label, std::string &path, const char *preset, bool stripExt)
    {
        // パスは手で打たせない（綴りを1文字間違えると読めず、原因に気づきにくい）。
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
     * @brief 時刻 t で映るショットを返す（再生と同じ「開始時刻が t 以下のうち最後」）。
     *
     * 編集中の写しは並び替えていないので、並び順に頼らず開始時刻で比べる。
     *
     * @return 映るショット。ショットが無ければ nullptr。
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

    // アニメのファイルを読み、長さの自動（0）に使うクリップの長さを入れる。
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
    // 1. ファイルの操作（上に横長で置く）
    // 2. 左のペイン: 全体の設定とプレビュー、項目を足す・消す、タイムライン
    // 3. 右のペイン: 選んでいる項目の数値

    // 【備考】
    // 項目を足す・消すのはタイムラインより先、数値はタイムラインより後に描く。
    // タイムラインが行と台本の項目の対応を作り直すので、足した項目を同じフレームで並べ、数値の欄はその対応で引くため。

    // 右のペインの幅。ベクトル3つの欄と「今の画面を取り込む」が1行に収まる幅
    constexpr float kInspectorWidth = 380.0f;

    // ---------------------------------------------------------
    // 1. ファイルの操作
    // ---------------------------------------------------------

    // 開いていなくても、何も無い新しい台本を編集できる状態から始める。
    DrawFileBar(context);
    ImGui::Separator();

    // ---------------------------------------------------------
    // 2. 左のペイン
    // ---------------------------------------------------------
    ImGui::BeginChild("edit", ImVec2(-(kInspectorWidth + 8.0f), 0.0f), true);
    m_dirty |= DrawScriptSettings();
    DrawPreviewBar(context);

    ImGui::Separator();
    m_dirty |= DrawItemButtons();
    m_dirty |= DrawTimeline();
    ImGui::EndChild();

    // ---------------------------------------------------------
    // 3. 右のペイン
    // ---------------------------------------------------------
    ImGui::SameLine();
    ImGui::BeginChild("inspector", ImVec2(0.0f, 0.0f), true);
    m_dirty |= DrawInspector(context);
    ImGui::EndChild();
}

void CinematicEditorWindow::OnFrameBegin(EditorContext &context)
{
    // 一時停止中だけ出す。動いている間に出すと、プレイヤーを操作しているのに画面がプレビューの絵のままになる。
    const bool active = GetVisible() && m_previewEnabled && context.isPaused;

    // 止めるのは、出していたときの1回だけ。
    if (active || m_previewPushed) PushPreview(context, active);
    m_previewPushed = active;
}

void CinematicEditorWindow::DrawFileBar(EditorContext &context)
{
    const bool hasFile = !m_script.path.empty(); // 一度でも保存した（ファイルがある）台本か

    if (ImGui::Button("新規")) NewScript();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("何も無い台本にします（保存していない変更は捨てます）");

    ImGui::SameLine();
    if (ImGui::Button("開く..."))
    {
        if (auto picked = FileDialog::OpenPreset("Cinematic")) Open(FileDialog::ConvertToRelativePath(*picked), false);
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(!hasFile);
    if (ImGui::Button("開き直す")) Open(m_script.path, true);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("保存していない変更を捨てて、ファイルの内容に戻します");

    ImGui::SameLine();
    if (ImGui::Button("保存")) Save(context);

    ImGui::SameLine();
    if (ImGui::Button("名前を付けて保存...")) SaveAs(context);

    // 再生はゲームと同じ再生要求で流す。読むのはゲーム側が持つ台本なので、保存してからでないと古い方が流れる。
    ImGui::SameLine();
    ImGui::BeginDisabled(m_dirty || !hasFile);
    if (ImGui::Button("ゲームで再生") && context.world)
        context.world->GetEventBus().Publish(CinematicPlayRequestEvent{m_script.path});
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("保存した台本を、ゲームと同じ再生要求で頭から流します（SE・エフェクト・アニメも出ます）\n"
                          "一時停止を解いてから押してください（一時停止中はトラックが進みません）");

    // 途中から流す。イベントの少し手前に再生位置を置いて押せば、そこだけを何度でも見直せる。
    ImGui::SameLine();
    if (ImGui::Button("ここから再生") && context.world)
        context.world->GetEventBus().Publish(CinematicPlayRequestEvent{m_script.path, m_time});
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("保存した台本を、タイムラインの再生位置（%.2f 秒）から流します\n"
                          "それより前の SE・エフェクト・シグナルは出さず、途中のアニメはその位置から始めます\n"
                          "開始待ちは飛ばします。一時停止を解いてから押してください",
                          m_time);

    ImGui::SameLine();
    ImGui::Text("%s", hasFile ? m_script.path.c_str() : "（新しい台本）");
    if (m_dirty)
    {
        ImGui::SameLine();
        ImGui::TextColored(kWarnColor, "（未保存）");
    }

    if (!m_status.empty()) ImGui::TextWrapped("%s", m_status.c_str());
}

bool CinematicEditorWindow::DrawScriptSettings()
{
    bool changed = false;
    ImGui::PushItemWidth(120.0f);
    changed |= DragSeconds("全体の長さ", m_script.duration, 0.1f);
    ImGui::SameLine();
    changed |= DragSeconds("開始待ち", m_script.startDelay);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("最初の絵で止めて、動き出すまで待つ時間");
    ImGui::SameLine();
    changed |= ImGui::Checkbox("長押しでスキップできる", &m_script.allowSkip);
    ImGui::PopItemWidth();
    return changed;
}

void CinematicEditorWindow::DrawPreviewBar(EditorContext &context)
{
    ImGui::Checkbox("ゲーム画面で絵を見る", &m_previewEnabled);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("一時停止中、再生位置のカメラの絵をゲーム画面に出します\n"
                          "カメラだけで、SE・エフェクト・アニメは出ません（それは「ゲームで再生」で確かめる）\n"
                          "基準の位置と向きは、今そこに居る対象のものを使います");

    if (!m_previewEnabled) return;

    ImGui::SameLine();
    if (!context.isPaused)
    {
        ImGui::TextDisabled("（一時停止中だけ出ます。Tab で一時停止）");
        return;
    }

    // 基準が居ないショットは絵を作れない。カメラ側は毎フレーム警告を出せないので、ここで知らせる。
    const CameraShot *shot = ShotAt(m_script.shots, m_time);
    if (shot && !shot->subject.empty() && context.world &&
        Cinematic::FindEntityByName(*context.world, shot->subject) == InvalidEntityID)
    {
        ImGui::TextColored(kWarnColor, "このショットの基準 '%s' がシーンに居ないので、絵を出せません", shot->subject.c_str());
    }
}

bool CinematicEditorWindow::DrawTimeline()
{
    int       frame  = CinematicTimelineSequence::TimeToFrame(m_time);
    const int before = frame;
    m_sequence->Begin(m_script, frame);

    // ImSequencer は項目が1つも無いと何も描かない。
    if (m_sequence->GetItemCount() == 0)
    {
        ImGui::TextDisabled("ショットもトラックもありません");
        return false;
    }

    ImGui::Text("再生位置 %.2f 秒", m_time);

    // ImSequencer は残りの高さを全部使うので、行数ぶんの高さの子ウィンドウに閉じ込める。
    //   目盛り(20) + 行(20 x 数) + 下の横スクロール(14) + 内側の余白(8) + 少しの余裕
    constexpr float kItemHeight = 20.0f;
    const float     height      = kItemHeight * (m_sequence->GetItemCount() + 1) + 14.0f + 8.0f + 4.0f;

    ImGui::BeginChild("timeline", ImVec2(0.0f, height));
    ImSequencer::Sequencer(m_sequence.get(), &frame, nullptr, &m_selectedRow, &m_firstFrame,
                           ImSequencer::SEQUENCER_EDIT_STARTEND | ImSequencer::SEQUENCER_CHANGE_FRAME);
    ImGui::EndChild();

    ImGui::TextDisabled("帯の端 : 伸び縮み / 帯の中 : 丸ごと移動 / 上の目盛り : 再生位置 / Ctrl+ホイール : 拡大");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("帯 = その項目が効いている時間（SE・エフェクト・シグナルは時刻の1コマ）\n"
                          "帯が明るい間 = 再生位置がその項目の中にある\n"
                          "目盛りの数字はコマ番号（1秒 = %d コマ）",
                          CinematicTimelineSequence::kFramesPerSecond);

    if (frame != before) m_time = CinematicTimelineSequence::FrameToTime(frame);

    return m_sequence->Flush();
}

bool CinematicEditorWindow::DrawItemButtons()
{
    // 足すときは再生位置に置く。全体の長さちょうどには置けない（読み込みが「映らない」と弾く）ので、1コマ手前までにする。
    const float lastTime = std::max(0.0f, m_script.duration - 1.0f / CinematicTimelineSequence::kFramesPerSecond);
    const float at       = std::clamp(m_time, 0.0f, lastTime);

    bool        added = false;
    RowKind     kind  = RowKind::Shot;
    std::size_t index = 0;

    ImGui::TextUnformatted("足す（再生位置に置く）:");

    ImGui::SameLine();
    if (ImGui::Button("ショット"))
    {
        // 再生位置で映っているショットを写して足す。一から数値を書くより、近い構図から直す方が早い。
        CameraShot shot;
        if (const CameraShot *base = ShotAt(m_script.shots, at)) shot = *base;
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

    // 足した項目を選んで、すぐ数値を直せるようにする。
    if (added) m_selectedRow = CinematicTimelineSequence::RowOf(m_script, kind, index);

    // 消すのは選んでいる項目。行の対応は前のフレームのタイムラインで作ったもの（台本はそれから変わっていない）。
    ImGui::SameLine(0.0f, 30.0f);
    CinematicTimelineSequence::Row row;
    const bool hasSelection = !added && m_sequence->RowAt(m_selectedRow, row);
    ImGui::BeginDisabled(!hasSelection);
    const bool erase = ImGui::Button("選んだ項目を消す");
    ImGui::EndDisabled();

    if (erase && hasSelection)
    {
        auto eraseAt = [](auto &items, std::size_t i) {
            if (i < items.size()) items.erase(items.begin() + static_cast<std::ptrdiff_t>(i));
        };
        switch (row.kind)
        {
        case RowKind::Shot: eraseAt(m_script.shots, row.index); break;
        case RowKind::Sound: eraseAt(m_script.sounds, row.index); break;
        case RowKind::Effect: eraseAt(m_script.effects, row.index); break;
        case RowKind::Signal: eraseAt(m_script.signals, row.index); break;
        case RowKind::Animation: eraseAt(m_script.animations, row.index); break;
        }
        m_selectedRow = -1;
        return true;
    }
    return added;
}

bool CinematicEditorWindow::DrawInspector(EditorContext &context)
{
    CinematicTimelineSequence::Row row;
    if (!m_sequence->RowAt(m_selectedRow, row))
    {
        ImGui::TextDisabled("帯を選ぶと、ここで数値を直せます");
        return false;
    }

    bool changed = false;
    ImGui::PushItemWidth(kFieldWidth);

    // 行の対応は、このフレームのタイムラインで作り直したもの（項目の足し消しはその前に済んでいる）。
    switch (row.kind)
    {
    case RowKind::Shot: changed = DrawShotFields(context, m_script.shots[row.index]); break;
    case RowKind::Sound: changed = DrawSoundFields(context, m_script.sounds[row.index]); break;
    case RowKind::Effect: changed = DrawEffectFields(context, m_script.effects[row.index]); break;
    case RowKind::Signal: changed = DrawSignalFields(m_script.signals[row.index]); break;
    case RowKind::Animation: changed = DrawAnimationFields(context, m_script.animations[row.index]); break;
    }

    ImGui::PopItemWidth();
    return changed;
}

// ---------------------------------------------------------------------------
// 種類ごとの数値の欄
// ---------------------------------------------------------------------------

bool CinematicEditorWindow::DrawShotFields(EditorContext &context, CameraShot &shot)
{
    bool changed = false;
    ImGui::TextUnformatted("ショット");

    changed |= DragSeconds("開始", shot.start);
    changed |= DragSeconds("長さ", shot.duration, 0.01f);
    changed |= DrawSubjectField(context, shot.subject, "ワールドの原点");
    changed |= ComboEnum("動かし方", kCameraMotionNames, shot.motion);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Truck = 開始位置から終了位置へまっすぐ / Orbit = 注視点の周りを回り込む");
    changed |= ComboEnum("速さのカーブ", kCameraEaseNames, shot.ease);
    changed |= DragSeconds("入りのブレンド", shot.blendIn);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("前のショットの絵から混ぜる時間。0 ならカット\nブレンドの間、このショットの動きは最初の絵で止まる");

    const bool orbit = (shot.motion == CameraMotion::Orbit);

    // 開始と終了は同じ並びで描く。
    for (int side = 0; side < 2; ++side)
    {
        const bool toEnd = (side == 1);
        ImGui::PushID(side);
        ImGui::SeparatorText(toEnd ? "終了" : "開始");

        if (ImGui::Button("今の画面を取り込む")) changed |= CaptureView(context, shot, toEnd);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("ゲーム画面に今映っているカメラの位置・向き・画角を、基準から見たずれに直して書きます\n"
                              "自由カメラで構図を決めてから押す（プレビュー中は、プレビューの絵がそのまま入る）");

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
        ImGui::PopID();
    }

    ImGui::TextDisabled(orbit ? "注視点は基準から、カメラは注視点の周り（角度 0=前, 90=右）。向きは基準の向きが基準"
                              : "ずれは基準の向きが基準（+X=右, +Y=上, +Z=前）");
    return changed;
}

bool CinematicEditorWindow::DrawSoundFields(EditorContext &context, CinematicSound &sound)
{
    bool changed = false;
    ImGui::TextUnformatted("SE");

    changed |= DragSeconds("時刻", sound.time);

    // 音は AudioConfig に登録した名前から選ぶ（アニメのシーケンサーの Sound と同じ出どころ）。
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
    ImGui::TextUnformatted("エフェクト");

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
    ImGui::TextUnformatted("シグナル");

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
    ImGui::TextUnformatted("アニメ");

    changed |= DragSeconds("開始", anim.start);
    changed |= DragSeconds("長さ", anim.duration);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 ならクリップ1回分（クリップの長さ ÷ 速さ）");
    ImGui::SameLine();
    ImGui::TextDisabled("実際 %.2f 秒", anim.Length());

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

    // 画面のカメラは向きしか持たないので、視線の上のどこを注視点にするかを決める。
    // 視線の上で基準に一番近い点にすると、Orbit の中心が基準の近くに来る。
    // 基準がカメラの後ろ（または近すぎる）なら、5m 先にする。
    float distance = (subjectPos - eye).Dot(front);
    if (distance < 1.0f) distance = 5.0f;
    const Vector3 look = eye + front * distance;

    // ---------------------------------------------------------
    // 4. 基準から見たずれ
    // ---------------------------------------------------------

    // 再生側は「ずれを基準の向きで回して足す」ので、逆に「引いてから逆向きに回す」。
    const Matrix toLocal = Matrix::CreateRotationY(-yaw);

    const Vector3 lookOffset = Vector3::TransformNormal(look - subjectPos, toLocal);
    (toEnd ? shot.lookEnd : shot.lookStart) = lookOffset;

    if (shot.motion == CameraMotion::Orbit)
    {
        // Orbit は注視点からのずれを、角度（0=前, 90=右）・水平の距離・高さに直す。
        const Vector3 o      = Vector3::TransformNormal(eye - look, toLocal);
        float         angle  = DirectX::XMConvertToDegrees(std::atan2(o.x, o.z));
        const float   other  = toEnd ? shot.angleStart : shot.angleEnd;

        // 角度は数値のまま補間されるので、反対側の端に一番近い表し方にする（-170 と 170 で1周近く回らないように）。
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

    // 正射影のカメラには画角が無い（0）ので、そのときは書かない。
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
        if (!active) return;

        cine->previewScript = m_script;
        cine->previewTime   = m_time;

        // カメラ側のショットの探し方は開始時刻の昇順を前提にする。
        // 編集中の写しはドラッグや追加で順が崩れているので、渡す写しだけ並べる（読み込みと同じく同時刻は書いた順）。
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

    // 読み込みで時刻の順へ並び直るので、選んでいた行は別の項目を指しうる。選び直してもらう。
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

    // ゲームが読むのは台本フォルダの直下だけ。それ以外へ保存すると、「ゲームで再生」しても見つからない。
    const std::string dir = std::string(kCinematicScriptDir) + "/";
    if (path.rfind(dir, 0) != 0 || path.find('/', dir.size()) != std::string::npos)
    {
        m_status = "台本は " + std::string(kCinematicScriptDir) + " の直下に保存してください（ゲームはそこだけを読む）: " + path;
        return;
    }

    // 保存に失敗したときは元の保存先へ戻す。新しい保存先を残すと、次の「保存」が失敗した先へ書きにいく。
    const std::string previous = m_script.path;
    m_script.path              = path;
    if (!Save(context)) m_script.path = previous;
}

bool CinematicEditorWindow::Save(EditorContext &context)
{
    // まだ一度も保存していない台本は、保存先を選ばせる。
    if (m_script.path.empty())
    {
        SaveAs(context);
        return !m_script.path.empty();
    }

    // 書けるかどうかは、読み込みと同じ決まりで CinematicScriptIO が確かめる。
    std::string err;
    if (!CinematicScriptIO::Save(m_script.path, m_script, err))
    {
        m_status = "保存できませんでした : " + err;
        return false;
    }

    // 読み直して、ゲームが読むのと同じ形（時刻の順・アニメの読み込み）にそろえる。
    const std::string path = m_script.path;
    Open(path, true);
    m_status = "保存しました : " + path;

    // ゲーム側が持つ台本の一覧も読み直させる。保存してすぐ「ゲームで再生」を押したときに、新しい台本で流すため。
    if (context.world) context.world->GetEventBus().Publish(CinematicScriptsChangedEvent{});
    return true;
}

#else // ENABLE_EDITOR

// Release ではつなぎの実体（CinematicTimelineSequence.cpp）をビルドしないので、
// unique_ptr が要求する「完全な型」だけをここで用意する。
class CinematicTimelineSequence
{
};

CinematicEditorWindow::CinematicEditorWindow() : EditorWindow("Cinematic Editor", false) {}
CinematicEditorWindow::~CinematicEditorWindow() = default;

void CinematicEditorWindow::DrawContents(EditorContext &) {}
void CinematicEditorWindow::OnFrameBegin(EditorContext &) {}
void CinematicEditorWindow::DrawFileBar(EditorContext &) {}
bool CinematicEditorWindow::DrawScriptSettings() { return false; }
void CinematicEditorWindow::DrawPreviewBar(EditorContext &) {}
bool CinematicEditorWindow::DrawTimeline() { return false; }
bool CinematicEditorWindow::DrawItemButtons() { return false; }
bool CinematicEditorWindow::DrawInspector(EditorContext &) { return false; }
bool CinematicEditorWindow::DrawShotFields(EditorContext &, CameraShot &) { return false; }
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
