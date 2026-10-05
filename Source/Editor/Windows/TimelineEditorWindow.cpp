// 役割・設計方針は TimelineEditorWindow.h を参照。
#include "TimelineEditorWindow.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/Common/CCL_Archetype.h"
#include "Engine/Timeline/Components/TimelinePlayerComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Camera/Components/VirtualCameraComponents.h"
#include "Engine/Core/Components/NameComponent.h"
#include "Engine/Core/Components/EditorTransientTag.h" // プレビュー用のものはシーンに保存しない
#include "Engine/Graphics/RHI/Camera.h"
#include "Engine/Audio/IAudioAPI.h"
#include "Engine/Platform/Core/Dialog.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Serialization/Factory/EntityFactory.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h" // std::string の InputText

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>

using namespace CCL::ECS;
using namespace DirectX;
using namespace DirectX::SimpleMath;

namespace
{
    namespace mi = ComponentMetaImGui;

    // ---- 大きさ（px） ----
    // 時間軸はウィンドウの幅に合わせて伸ばさない。全体の長さがいつもこの幅に収まるよう縮尺を変える
    constexpr float  kLabelWidth = 110.0f; // 行の名前の欄
    constexpr float  kTrackWidth = 560.0f; // 時間軸の幅
    constexpr float  kRulerH     = 22.0f;  // 目盛りの高さ
    constexpr float  kLaneH      = 26.0f;  // 1行の高さ
    constexpr float  kFieldWidth = 240.0f; // 詳細の欄の幅
    constexpr ImVec2 kWindowSize = {kLabelWidth + kTrackWidth + 40.0f, 620.0f};

    constexpr int kLaneCount = static_cast<int>(TimelineClipKind::Count);

    // ---- 時間 ----
    constexpr float kSnapSeconds   = 0.05f; // ドラッグの刻み（Shift で刻みなし）
    constexpr float kMinClipLength = 0.05f; // 区間のクリップの最短の長さ
    constexpr float kTailSeconds   = 1.0f;  // 時間軸の右端に足す余白（最後のクリップを伸ばせるように）
    constexpr float kMinSpan       = 5.0f;  // 時間軸の最短の長さ
    constexpr float kEdgeGrab      = 6.0f;  // 右端を掴める幅(px)
    constexpr float kPointGrab     = 7.0f;  // 点のクリップを掴める半径(px)

    // 新しく足すクリップの既定の長さ(秒)
    constexpr float kNewCameraSeconds    = 2.0f;
    constexpr float kNewAnimationSeconds = 1.0f;

    // 種類ごとの名前と色。並びは TimelineClipKind の宣言順
    constexpr const char *kKindNames[]  = {"カメラ", "アニメ", "エフェクト", "SE", "シグナル"};
    constexpr ImU32       kKindColors[] = {
        IM_COL32(80, 140, 230, 255),  // カメラ
        IM_COL32(90, 190, 120, 255),  // アニメ
        IM_COL32(230, 160, 60, 255),  // エフェクト
        IM_COL32(200, 110, 200, 255), // SE
        IM_COL32(220, 80, 80, 255),   // シグナル
    };

    // 区切りを '/' にそろえ、先頭の "./" を落とす。プレハブには ".\\Assets\\..." 形式も混じっている
    std::string NormalizePath(std::string p)
    {
        std::replace(p.begin(), p.end(), '\\', '/');
        if (p.rfind("./", 0) == 0) p.erase(0, 2);
        return p;
    }

    bool SamePath(const std::string &a, const std::string &b)
    {
        return !a.empty() && NormalizePath(a) == NormalizePath(b);
    }

    // パスの最後の部分（拡張子なし）。ラベルに長いパスを出すと読めない
    std::string Stem(const std::string &path) { return std::filesystem::path(path).stem().string(); }

    // 拡張子 .json を落とす（VFX のプレハブは拡張子なしで指定する決まり）
    std::string StripJsonExt(std::string p)
    {
        const std::string ext = ".json";
        if (p.size() > ext.size() && p.compare(p.size() - ext.size(), ext.size(), ext) == 0)
            p.resize(p.size() - ext.size());
        return p;
    }

    // ドラッグの刻みにそろえる。Shift を押している間は刻まない（細かく合わせたいとき）
    float Snap(float t)
    {
        if (ImGui::GetIO().KeyShift) return t;
        return std::round(t / kSnapSeconds) * kSnapSeconds;
    }

    // 列挙値のコンボ。変わったら true
    template <class Enum, std::size_t N>
    bool EnumCombo(const char *label, Enum &value, const char *const (&names)[N])
    {
        int idx = static_cast<int>(value);
        if (!ImGui::Combo(label, &idx, names, static_cast<int>(N))) return false;
        value = static_cast<Enum>(idx);
        return true;
    }

    // 役割名の欄。よく使う名前はボタンで入れられるようにする（打ち間違うと黙って結び付かない）
    bool RoleField(const char *label, std::string &role, bool allowEmpty)
    {
        bool changed = false;
        ImGui::PushID(label);
        if (ImGui::RadioButton("Player", role == "Player")) { role = "Player"; changed = true; }
        ImGui::SameLine();
        if (ImGui::RadioButton("Boss", role == "Boss")) { role = "Boss"; changed = true; }
        if (allowEmpty)
        {
            ImGui::SameLine();
            if (ImGui::RadioButton("なし", role.empty())) { role.clear(); changed = true; }
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(label);
        ImGui::PopID();
        return changed;
    }
} // namespace

TimelineEditorWindow::TimelineEditorWindow()
    : EditorWindow("Timeline Editor", false)
{
}

void TimelineEditorWindow::OnBeforeBegin()
{
    // 初めて開いたときだけ大きさを決める。後でユーザーが変えた大きさは ImGui が覚えている
    ImGui::SetNextWindowSize(kWindowSize, ImGuiCond_FirstUseEver);
}

// ============================================================
// 全体
// ============================================================

void TimelineEditorWindow::DrawContents(EditorContext &context)
{
    if (m_loaded) EnsurePreviewPlayer(context);

    DrawToolbar(context);
    DrawDiscardPopup();

    if (!m_loaded)
    {
        ImGui::Spacing();
        ImGui::TextDisabled("「開く」でタイムライン（Assets/Timelines/*.json）を選んでください");
        return;
    }

    DrawRoleWarning(context);
    ImGui::Spacing();
    DrawTimeline(context);

    ImGui::Separator();
    DrawInspector(context);

    // 文字の入力中でなければ、Delete キーで選んだクリップを消す
    if (m_selected.IsValid() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
        DeleteSelected();

    // 編集は1フレームにまとめて1回だけ渡す
    if (m_needsApply) ApplyToPlayer(context);
}

// ============================================================
// 上の帯：ファイルと再生
// ============================================================

void TimelineEditorWindow::DrawToolbar(EditorContext &context)
{
    if (ImGui::Button("新規")) RequestAction(PendingAction::New);
    ImGui::SameLine();
    if (ImGui::Button("開く")) RequestAction(PendingAction::Open);
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_loaded);
    if (ImGui::Button("保存")) SaveFile(false);
    ImGui::SameLine();
    if (ImGui::Button("別名で保存")) SaveFile(true);
    ImGui::EndDisabled();

    if (!m_loaded) return;

    ImGui::SameLine();
    ImGui::Text("  %s%s", m_path.empty() ? "(未保存)" : Stem(m_path).c_str(), m_dirty ? " *" : "");
    if (ImGui::IsItemHovered() && !m_path.empty()) ImGui::SetTooltip("%s", m_path.c_str());

    // 再生の操作。プレビュー先が無いときは押せない（置いたばかりで World に現れる前など）
    TimelinePlayerComponent *player = PreviewPlayer(context);
    ImGui::BeginDisabled(!player);
    if (ImGui::Button("最初から再生") && player)
    {
        ApplyToPlayer(context);
        player->RequestPlay();
    }
    ImGui::SameLine();
    if (ImGui::Button("止める")) StopPreview(context);
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::Text("  %.2f / %.2f 秒", m_time, m_buffer.duration);
}

void TimelineEditorWindow::RequestAction(PendingAction action)
{
    // 変更が無ければ確かめずにすぐ行う
    if (!m_dirty)
    {
        if (action == PendingAction::New) NewFile();
        if (action == PendingAction::Open) OpenFile();
        return;
    }
    m_pending = action;
    ImGui::OpenPopup("DiscardTimelineChanges");
}

void TimelineEditorWindow::DrawDiscardPopup()
{
    if (!ImGui::BeginPopupModal("DiscardTimelineChanges", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    ImGui::Text("保存していない変更があります。捨てて続けますか？");
    if (ImGui::Button("捨てて続ける"))
    {
        const PendingAction action = m_pending;
        m_pending                  = PendingAction::None;
        m_dirty                    = false;
        ImGui::CloseCurrentPopup();
        if (action == PendingAction::New) NewFile();
        if (action == PendingAction::Open) OpenFile();
    }
    ImGui::SameLine();
    if (ImGui::Button("やめる"))
    {
        m_pending = PendingAction::None;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void TimelineEditorWindow::NewFile()
{
    auto pathOpt = FileDialog::SavePreset("Timeline", "タイムラインを新規作成");
    if (!pathOpt) return;

    // 空だと何から始めればよいか分からないので、プレイヤーを映すショットを1本置く
    TimelineAsset  asset;
    CameraShotClip shot;
    shot.subject  = "Player";
    shot.duration = kNewCameraSeconds;
    asset.cameraShots.push_back(shot);
    TimelineAssetLoader::Normalize(asset);

    const std::string path = NormalizePath(FileDialog::ConvertToRelativePath(*pathOpt));
    std::string       err;
    if (!TimelineAssetLoader::Save(path, asset, err))
    {
        CCL_LOG_ERROR(LogCategory::Editor, "[TimelineEditor] 新規作成に失敗: %s (%s)", path.c_str(), err.c_str());
        return;
    }

    m_buffer     = std::move(asset);
    m_path       = path;
    m_loaded     = true;
    m_dirty      = false;
    m_selected   = {TimelineClipKind::Camera, 0};
    m_time       = 0.0f;
    m_needsApply = true;
}

void TimelineEditorWindow::OpenFile()
{
    auto pathOpt = FileDialog::OpenPreset("Timeline", "タイムラインを開く");
    if (!pathOpt) return;

    const std::string path = NormalizePath(FileDialog::ConvertToRelativePath(*pathOpt));
    TimelineAsset     asset;
    std::string       err;
    if (!TimelineAssetLoader::Load(path, asset, err))
    {
        CCL_LOG_ERROR(LogCategory::Editor, "[TimelineEditor] 読み込みに失敗: %s (%s)", path.c_str(), err.c_str());
        return;
    }
    // 長さを省略したアニメの帯が潰れないよう、クリップの長さを埋めておく
    TimelineAssetLoader::ResolveAnimations(asset);
    TimelineAssetLoader::Normalize(asset);

    m_buffer     = std::move(asset);
    m_path       = path;
    m_loaded     = true;
    m_dirty      = false;
    m_selected   = {};
    m_time       = 0.0f;
    m_needsApply = true;
}

bool TimelineEditorWindow::SaveFile(bool saveAs)
{
    std::string path = m_path;
    if (saveAs || path.empty())
    {
        auto pathOpt = FileDialog::SavePreset("Timeline", "タイムラインを保存");
        if (!pathOpt) return false;
        path = NormalizePath(FileDialog::ConvertToRelativePath(*pathOpt));
    }

    // 保存するのは並べ直したコピー。バッファ自体を並べ替えると、選んでいるクリップの番号がずれる
    TimelineAsset copy = m_buffer;
    TimelineAssetLoader::Normalize(copy);

    std::string err;
    if (!TimelineAssetLoader::Save(path, copy, err))
    {
        CCL_LOG_ERROR(LogCategory::Editor, "[TimelineEditor] 保存に失敗: %s (%s)", path.c_str(), err.c_str());
        return false;
    }

    CCL_LOG_INFO(LogCategory::Editor, "[TimelineEditor] 保存: %s", path.c_str());
    m_path  = path;
    m_dirty = false;
    return true;
}

// ============================================================
// プレビュー先
// ============================================================

TimelinePlayerComponent *TimelineEditorWindow::PreviewPlayer(EditorContext &context) const
{
    if (!context.world || !m_loaded || !context.world->IsEntityValid(m_player)) return nullptr;
    auto *player = context.world->GetComponent<TimelinePlayerComponent>(m_player);
    if (!player || !SamePath(player->assetPath, m_path)) return nullptr;
    return player;
}

void TimelineEditorWindow::EnsurePreviewPlayer(EditorContext &context)
{
    if (!context.world || m_path.empty() || PreviewPlayer(context)) return;

    World &w = *context.world;

    // 1. 編集中のファイルを再生するものが居ればそれを使う（GameScene の導入ムービーなど）
    for (const auto &chunkPtr : w.GetChunkManager().GetChunks())
    {
        if (!chunkPtr || !chunkPtr->HasComponent<TimelinePlayerComponent>()) continue;
        for (size_t i = 0; i < chunkPtr->GetEntityCount(); ++i)
        {
            if (chunkPtr->IsEntityDestroyed(i)) continue;
            const EntityID id = chunkPtr->GetEntityByIndex(i);
            const auto    *p  = w.GetComponent<TimelinePlayerComponent>(id);
            if (p && SamePath(p->assetPath, m_path))
            {
                m_player     = id;
                m_needsApply = true;
                return;
            }
        }
    }

    // 2. 前に置いた一時的なものが残っていれば、編集中のファイルへ向け直す（増やさない）
    if (w.IsEntityValid(m_spawned))
    {
        if (auto *p = w.GetComponent<TimelinePlayerComponent>(m_spawned))
        {
            p->assetPath    = m_path;
            p->asset.reset();
            p->hasLoadError = false;
            m_player        = m_spawned;
            m_needsApply    = true;
            return;
        }
    }

    // 3. 置いたばかりで、まだ World に現れていない。待たずに置くと毎フレーム増える
    if (m_spawnWait > 0)
    {
        --m_spawnWait;
        return;
    }

    // 4. 一時的なものを置く（シーンを保存しても書き出されない。シーンを読み込み直すと消える）
    auto arch = ArchetypeHelper::Generate<TransformComponent, VirtualCamera, TimelinePlayerComponent, NameComponent,
                                          Tag::EditorTransientTag>();
    auto e    = EntityFactory::SpawnNamed(w, "TimelinePreview", arch);

    TimelinePlayerComponent tp;
    tp.assetPath     = m_path;
    tp.playOnStart   = false; // 置いた瞬間に流れ出さないように。再生はボタンかスクラブで
    tp.autoPlayArmed = false;

    // 導入ムービーのプレハブ（BossIntroTimeline.json）と同じ設定。カメラは再生中だけ優先度が上がる
    VirtualCamera vcam;
    vcam.priority  = tp.idlePriority;
    vcam.blendTime = 0.0f;
    vcam.farClip   = 3000.0f;

    TransformComponent tr;
    tr.isStatic = false;

    e.Set(tr).Set(vcam).Set(tp).Set(Tag::EditorTransientTag{});

    m_spawned    = e.ID();
    m_player     = m_spawned;
    m_spawnWait  = 30;
    m_needsApply = true;
}

void TimelineEditorWindow::ApplyToPlayer(EditorContext &context)
{
    TimelinePlayerComponent *player = PreviewPlayer(context);
    if (!player) return; // 渡せる相手が現れたら渡す（m_needsApply は立てたまま）
    m_needsApply = false;

    // 再生側は時刻の昇順とアニメの解決を前提にしているので、整えたコピーを渡す
    TimelineAsset copy = m_buffer;
    TimelineAssetLoader::ResolveAnimations(copy);
    TimelineAssetLoader::Normalize(copy);

    player->asset        = std::make_shared<const TimelineAsset>(std::move(copy));
    player->hasLoadError = false;
    // ショットの基準を取り直させる（編集で対象や座標系を替えたとき、古い基準のまま評価しないように）
    player->activeShot  = -1;
    player->hasLastPose = false;
}

void TimelineEditorWindow::Scrub(EditorContext &context, float time)
{
    m_time = time;

    TimelinePlayerComponent *player = PreviewPlayer(context);
    if (!player) return;
    if (m_needsApply) ApplyToPlayer(context);

    // previewing の間は再生位置を自動で進めず、この時刻の絵を出し続ける
    player->previewing = true;
    player->time       = time;
    player->prevTime   = time;
}

void TimelineEditorWindow::StopPreview(EditorContext &context)
{
    TimelinePlayerComponent *player = PreviewPlayer(context);
    if (!player) return;

    // 終わった扱いにすると、カメラは待機優先度へ落ちてプレイヤーカメラへ戻り、握っているアニメも返す
    player->previewing = false;
    player->state      = TimelinePlayState::Finished;
}

void TimelineEditorWindow::DrawRoleWarning(EditorContext &context)
{
    TimelinePlayerComponent *player = PreviewPlayer(context);
    if (!player) return;

    // 役割が結び付いていないとカメラの絵が出ない。黙って何も映らないと原因が分からないので、欠けを出す
    std::string missing;
    for (const auto &role : m_buffer.CollectRoles())
    {
        const auto it = player->bindings.find(role);
        if (it == player->bindings.end() || !context.world->IsEntityValid(it->second))
            missing += (missing.empty() ? "" : ", ") + role;
    }
    if (missing.empty()) return;

    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "! %s がまだ見つかりません", missing.c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("プレイヤー・ボスがシーンにいて、ゲームが一時停止していなければ自動で見つかります");
}

// ============================================================
// 時間軸
// ============================================================

std::vector<TimelineEditorWindow::ClipView> TimelineEditorWindow::CollectClips() const
{
    std::vector<ClipView> views;

    for (int i = 0; i < static_cast<int>(m_buffer.cameraShots.size()); ++i)
    {
        const auto &c = m_buffer.cameraShots[i];
        views.push_back({{TimelineClipKind::Camera, i}, c.start, c.duration, true,
                         std::string(kCameraShotMotionNames[static_cast<int>(c.motion)]) + " " + c.subject});
    }
    for (int i = 0; i < static_cast<int>(m_buffer.animations.size()); ++i)
    {
        const auto &c = m_buffer.animations[i];
        views.push_back({{TimelineClipKind::Animation, i}, c.start, c.duration, true,
                         c.subject + " " + Stem(c.sequence)});
    }
    for (int i = 0; i < static_cast<int>(m_buffer.effects.size()); ++i)
    {
        // 継続放出の長さがあれば帯、無ければ点
        const auto &c = m_buffer.effects[i];
        views.push_back({{TimelineClipKind::Effect, i}, c.time, c.duration, c.duration > 0.0f, Stem(c.effect)});
    }
    for (int i = 0; i < static_cast<int>(m_buffer.sounds.size()); ++i)
    {
        const auto &c = m_buffer.sounds[i];
        views.push_back({{TimelineClipKind::Sound, i}, c.time, 0.0f, false, c.event});
    }
    for (int i = 0; i < static_cast<int>(m_buffer.signals.size()); ++i)
    {
        const auto &c = m_buffer.signals[i];
        views.push_back({{TimelineClipKind::Signal, i}, c.time, 0.0f, false, c.name + " " + c.param});
    }
    return views;
}

void TimelineEditorWindow::MoveClip(const TimelineClipRef &ref, float start)
{
    start = (std::max)(start, 0.0f);

    float *target = nullptr;
    switch (ref.kind)
    {
    case TimelineClipKind::Camera: target = &m_buffer.cameraShots[ref.index].start; break;
    case TimelineClipKind::Animation: target = &m_buffer.animations[ref.index].start; break;
    case TimelineClipKind::Effect: target = &m_buffer.effects[ref.index].time; break;
    case TimelineClipKind::Sound: target = &m_buffer.sounds[ref.index].time; break;
    case TimelineClipKind::Signal: target = &m_buffer.signals[ref.index].time; break;
    default: return;
    }
    if (*target == start) return; // 動いていないフレームは編集扱いにしない
    *target = start;
    MarkEdited();
}

void TimelineEditorWindow::ResizeClip(const TimelineClipRef &ref, float length)
{
    float *target = nullptr;
    switch (ref.kind)
    {
    case TimelineClipKind::Camera: target = &m_buffer.cameraShots[ref.index].duration; break;
    case TimelineClipKind::Animation: target = &m_buffer.animations[ref.index].duration; break;
    case TimelineClipKind::Effect:
        // エフェクトは長さ 0（バーストだけ）を許す。縮めきったら点に戻る
        target = &m_buffer.effects[ref.index].duration;
        if (length < kMinClipLength) length = 0.0f;
        break;
    default: return;
    }
    if (ref.kind != TimelineClipKind::Effect) length = (std::max)(length, kMinClipLength);
    if (*target == length) return;
    *target = length;
    MarkEdited();
}

void TimelineEditorWindow::DrawTimeline(EditorContext &context)
{
    TimelinePlayerComponent *player = PreviewPlayer(context);

    // 普通に再生している間は、再生位置の線を再生に付いていかせる
    if (player && !player->previewing && player->state == TimelinePlayState::Playing && m_drag != DragMode::Scrub)
        m_time = player->time;

    // 全体の長さがいつも kTrackWidth に収まるよう、1秒あたりの幅を決める
    const float span = (std::max)(m_buffer.duration + kTailSeconds, kMinSpan);
    const float pps  = kTrackWidth / span;

    ImDrawList  *dl      = ImGui::GetWindowDrawList();
    const ImVec2 origin  = ImGui::GetCursorScreenPos();
    const float  trackX  = origin.x + kLabelWidth;
    const float  lanesY  = origin.y + kRulerH;
    const float  height  = kRulerH + kLaneH * kLaneCount;
    const float  bottomY = origin.y + height;
    auto         toX     = [&](float t) { return trackX + t * pps; };

    // ---- 1. 行の名前と、その行にクリップを足す「+」 ----
    for (int lane = 0; lane < kLaneCount; ++lane)
    {
        ImGui::SetCursorScreenPos(ImVec2(origin.x, lanesY + lane * kLaneH + 3.0f));
        ImGui::PushID(lane);
        if (ImGui::SmallButton("+")) AddClip(static_cast<TimelineClipKind>(lane));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("再生位置に%sを足す", kKindNames[lane]);
        ImGui::SameLine();
        ImGui::TextUnformatted(kKindNames[lane]);
        ImGui::PopID();
    }

    // ---- 2. 時間軸の入力を受ける見えないボタン（描画は下で ImDrawList に直接行う） ----
    ImGui::SetCursorScreenPos(ImVec2(trackX, origin.y));
    ImGui::InvisibleButton("##timeline", ImVec2(kTrackWidth, height));
    const bool   hovered = ImGui::IsItemHovered();
    const bool   clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const ImVec2 mouse   = ImGui::GetIO().MousePos;
    const float  mouseT  = std::clamp((mouse.x - trackX) / pps, 0.0f, span);

    // ---- 3. 背景と目盛り ----
    dl->AddRectFilled(ImVec2(trackX, origin.y), ImVec2(trackX + kTrackWidth, bottomY), IM_COL32(28, 28, 32, 255));
    for (int lane = 0; lane < kLaneCount; ++lane)
    {
        const float y = lanesY + lane * kLaneH;
        if (lane % 2 == 1)
            dl->AddRectFilled(ImVec2(trackX, y), ImVec2(trackX + kTrackWidth, y + kLaneH), IM_COL32(255, 255, 255, 8));
        dl->AddLine(ImVec2(trackX, y), ImVec2(trackX + kTrackWidth, y), IM_COL32(255, 255, 255, 25));
    }

    // 数字を書く間隔は、隣の数字と 40px 以上離れる最小のきりのいい秒数にする
    float major = 1.0f;
    for (float step : {1.0f, 2.0f, 5.0f, 10.0f, 30.0f})
    {
        major = step;
        if (step * pps >= 40.0f) break;
    }
    for (float t = 0.0f; t <= span + 0.001f; t += major * 0.5f)
    {
        const bool  isMajor = std::fmod(t + 0.001f, major) < 0.01f;
        const float x       = toX(t);
        dl->AddLine(ImVec2(x, origin.y + (isMajor ? 8.0f : 14.0f)), ImVec2(x, isMajor ? bottomY : lanesY),
                    IM_COL32(255, 255, 255, isMajor ? 40 : 60));
        if (isMajor)
        {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.0fs", t);
            dl->AddText(ImVec2(x + 3.0f, origin.y + 2.0f), IM_COL32(200, 200, 200, 255), buf);
        }
    }

    // ---- 4. クリップ ----
    const std::vector<ClipView> clips = CollectClips();

    // 帯・点の当たり判定の範囲（描くのと掴むのを同じ式にする）
    struct Hit
    {
        float x0, x1;
        bool  isPoint;
    };
    auto hitOf = [&](const ClipView &v) {
        const float x0 = toX(v.start);
        if (v.length <= 0.0f) return Hit{x0, x0, true};
        return Hit{x0, (std::max)(toX(v.start + v.length), x0 + 4.0f), false};
    };

    const ClipView *hoveredClip = nullptr;
    for (const auto &v : clips)
    {
        const int   lane = static_cast<int>(v.ref.kind);
        const float y0   = lanesY + lane * kLaneH + 4.0f;
        const float y1   = y0 + kLaneH - 8.0f;
        const Hit   h    = hitOf(v);
        const bool  sel  = (v.ref == m_selected);
        const ImU32 col  = kKindColors[lane];

        if (h.isPoint)
        {
            // 点は菱形。名前は右に小さく添える
            const float cy = (y0 + y1) * 0.5f;
            const float r  = 6.0f;
            dl->AddQuadFilled(ImVec2(h.x0, cy - r), ImVec2(h.x0 + r, cy), ImVec2(h.x0, cy + r), ImVec2(h.x0 - r, cy), col);
            if (sel)
                dl->AddQuad(ImVec2(h.x0, cy - r), ImVec2(h.x0 + r, cy), ImVec2(h.x0, cy + r), ImVec2(h.x0 - r, cy),
                            IM_COL32_WHITE, 2.0f);
            dl->PushClipRect(ImVec2(trackX, y0), ImVec2(trackX + kTrackWidth, y1), true);
            dl->AddText(ImVec2(h.x0 + r + 3.0f, y0 + 1.0f), IM_COL32(220, 220, 220, 200), v.label.c_str());
            dl->PopClipRect();
        }
        else
        {
            dl->AddRectFilled(ImVec2(h.x0, y0), ImVec2(h.x1, y1), col, 3.0f);
            if (sel) dl->AddRect(ImVec2(h.x0, y0), ImVec2(h.x1, y1), IM_COL32_WHITE, 3.0f, 0, 2.0f);
            // 右端の取っ手（掴めば長さが変わることを見せる）
            if (v.resizable)
                dl->AddRectFilled(ImVec2(h.x1 - 3.0f, y0 + 3.0f), ImVec2(h.x1 - 1.0f, y1 - 3.0f), IM_COL32(255, 255, 255, 120));
            dl->PushClipRect(ImVec2(h.x0 + 3.0f, y0), ImVec2(h.x1 - 4.0f, y1), true);
            dl->AddText(ImVec2(h.x0 + 5.0f, y0 + 1.0f), IM_COL32(20, 20, 20, 255), v.label.c_str());
            dl->PopClipRect();
        }

        // マウスが乗っているクリップ（後に描いたものが手前なので、最後に当たったものを採る）
        if (hovered && mouse.y >= y0 - 2.0f && mouse.y <= y1 + 2.0f)
        {
            const bool inside = h.isPoint ? std::abs(mouse.x - h.x0) <= kPointGrab
                                          : (mouse.x >= h.x0 - 2.0f && mouse.x <= h.x1 + 2.0f);
            if (inside) hoveredClip = &v;
        }
    }

    // ---- 5. 掴む ----
    const bool onEdge = hoveredClip && hoveredClip->resizable && mouse.x >= hitOf(*hoveredClip).x1 - kEdgeGrab;
    if (hovered && onEdge) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (hovered && hoveredClip && m_drag == DragMode::None) ImGui::SetTooltip("%s", hoveredClip->label.c_str());

    if (clicked)
    {
        if (mouse.y < lanesY || !hoveredClip)
        {
            // 目盛りか空いた所: 再生位置を動かす
            m_drag = DragMode::Scrub;
        }
        else
        {
            m_selected   = hoveredClip->ref;
            m_dragRef    = hoveredClip->ref;
            m_drag       = onEdge ? DragMode::Resize : DragMode::Move;
            m_dragOffset = mouseT - hoveredClip->start;
        }
    }

    if (m_drag != DragMode::None)
    {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            m_drag = DragMode::None;
        }
        else
        {
            switch (m_drag)
            {
            case DragMode::Scrub: Scrub(context, Snap(mouseT)); break;
            case DragMode::Move: MoveClip(m_dragRef, Snap(mouseT - m_dragOffset)); break;
            case DragMode::Resize:
            {
                // 開始は掴んだ時点のクリップから引く（CollectClips は毎フレーム作り直すので参照を持たない）
                float start = 0.0f;
                for (const auto &v : clips)
                    if (v.ref == m_dragRef) start = v.start;
                ResizeClip(m_dragRef, Snap(mouseT) - start);
                break;
            }
            default: break;
            }
        }
    }

    // ---- 6. 再生位置の線 ----
    const float px = toX(std::clamp(m_time, 0.0f, span));
    dl->AddLine(ImVec2(px, origin.y), ImVec2(px, bottomY), IM_COL32(255, 70, 70, 255), 2.0f);
    dl->AddTriangleFilled(ImVec2(px - 5.0f, origin.y), ImVec2(px + 5.0f, origin.y), ImVec2(px, origin.y + 7.0f),
                          IM_COL32(255, 70, 70, 255));

    // 部品の下へカーソルを戻す（次のウィジェットが時間軸に重ならないように）
    ImGui::SetCursorScreenPos(ImVec2(origin.x, bottomY + 4.0f));
    ImGui::TextDisabled("クリック: 選ぶ / ドラッグ: 動かす / 右端: 長さ / 空いた所・目盛り: 再生位置 / Shift: 細かく");
}

void TimelineEditorWindow::AddClip(TimelineClipKind kind)
{
    switch (kind)
    {
    case TimelineClipKind::Camera: {
        CameraShotClip c;
        c.start    = m_time;
        c.duration = kNewCameraSeconds;
        c.subject  = "Player";
        m_buffer.cameraShots.push_back(c);
        m_selected = {kind, static_cast<int>(m_buffer.cameraShots.size()) - 1};
        break;
    }
    case TimelineClipKind::Animation: {
        AnimationClip c;
        c.start    = m_time;
        c.duration = kNewAnimationSeconds;
        c.subject  = "Boss";
        m_buffer.animations.push_back(c);
        m_selected = {kind, static_cast<int>(m_buffer.animations.size()) - 1};
        break;
    }
    case TimelineClipKind::Effect: {
        EffectClip c;
        c.time    = m_time;
        c.subject = "Boss";
        m_buffer.effects.push_back(c);
        m_selected = {kind, static_cast<int>(m_buffer.effects.size()) - 1};
        break;
    }
    case TimelineClipKind::Sound: {
        SoundClip c;
        c.time = m_time;
        m_buffer.sounds.push_back(c);
        m_selected = {kind, static_cast<int>(m_buffer.sounds.size()) - 1};
        break;
    }
    case TimelineClipKind::Signal: {
        SignalClip c;
        c.time = m_time;
        c.name = "Telop";
        m_buffer.signals.push_back(c);
        m_selected = {kind, static_cast<int>(m_buffer.signals.size()) - 1};
        break;
    }
    default: return;
    }
    MarkEdited();
}

void TimelineEditorWindow::DeleteSelected()
{
    auto erase = [&](auto &vec) {
        if (m_selected.index >= 0 && m_selected.index < static_cast<int>(vec.size()))
            vec.erase(vec.begin() + m_selected.index);
    };
    switch (m_selected.kind)
    {
    case TimelineClipKind::Camera: erase(m_buffer.cameraShots); break;
    case TimelineClipKind::Animation: erase(m_buffer.animations); break;
    case TimelineClipKind::Effect: erase(m_buffer.effects); break;
    case TimelineClipKind::Sound: erase(m_buffer.sounds); break;
    case TimelineClipKind::Signal: erase(m_buffer.signals); break;
    default: break;
    }
    m_selected = {};
    m_drag     = DragMode::None; // 消したクリップを掴んだままにしない
    MarkEdited();
}

void TimelineEditorWindow::MarkEdited()
{
    // 全体の長さはクリップの終わりから決まるので、時間軸の縮尺も追従させる。
    // バッファ自体は並べ替えない（選んでいるクリップの番号がずれる）ので、コピーで計算する
    if (!m_buffer.durationExplicit)
    {
        TimelineAsset copy = m_buffer;
        TimelineAssetLoader::Normalize(copy);
        m_buffer.duration = copy.duration;
    }
    m_dirty      = true;
    m_needsApply = true;
}

// ============================================================
// 詳細
// ============================================================

void TimelineEditorWindow::DrawInspector(EditorContext &context)
{
    bool changed = false;

    if (!m_selected.IsValid())
    {
        ImGui::TextDisabled("時間軸のクリップをクリックすると、ここで編集できます");
    }
    else
    {
        ImGui::Text("%s %d", kKindNames[static_cast<int>(m_selected.kind)], m_selected.index + 1);
        ImGui::SameLine();
        if (ImGui::SmallButton("削除 (Del)"))
        {
            DeleteSelected();
            return;
        }

        ImGui::PushID(static_cast<int>(m_selected.kind) * 10000 + m_selected.index);
        ImGui::PushItemWidth(kFieldWidth);
        switch (m_selected.kind)
        {
        case TimelineClipKind::Camera:
            if (m_selected.index < static_cast<int>(m_buffer.cameraShots.size()))
                changed |= DrawCameraShot(context, m_buffer.cameraShots[m_selected.index]);
            break;
        case TimelineClipKind::Animation:
            if (m_selected.index < static_cast<int>(m_buffer.animations.size()))
                changed |= DrawAnimation(m_buffer.animations[m_selected.index]);
            break;
        case TimelineClipKind::Effect:
            if (m_selected.index < static_cast<int>(m_buffer.effects.size()))
                changed |= DrawEffect(m_buffer.effects[m_selected.index]);
            break;
        case TimelineClipKind::Sound:
            if (m_selected.index < static_cast<int>(m_buffer.sounds.size()))
                changed |= DrawSound(context, m_buffer.sounds[m_selected.index]);
            break;
        case TimelineClipKind::Signal:
            if (m_selected.index < static_cast<int>(m_buffer.signals.size()))
                changed |= DrawSignal(m_buffer.signals[m_selected.index]);
            break;
        default: break;
        }
        ImGui::PopItemWidth();
        ImGui::PopID();
    }

    // 資産全体の設定。普段は触らないので畳んでおく
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("タイムライン全体の設定"))
    {
        ImGui::PushItemWidth(kFieldWidth);
        changed |= mi::InputText("メモ", m_buffer.comment);
        changed |= ImGui::Checkbox("全体の長さを固定する", &m_buffer.durationExplicit);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("切っておくと、一番後ろのクリップの終わりが全体の長さになる（普段はこちら）");
        if (m_buffer.durationExplicit)
            changed |= ImGui::DragFloat("全体の長さ(秒)", &m_buffer.duration, 0.05f, 0.1f, 120.0f, "%.2f");
        ImGui::PopItemWidth();
    }

    if (changed) MarkEdited();
}

bool TimelineEditorWindow::DrawCameraShot(EditorContext &context, CameraShotClip &c)
{
    bool changed = false;
    changed |= ImGui::DragFloat("開始(秒)", &c.start, 0.01f, 0.0f, 120.0f, "%.2f");
    changed |= ImGui::DragFloat("長さ(秒)", &c.duration, 0.01f, 0.05f, 60.0f, "%.2f");
    changed |= RoleField("を映す", c.subject, true);
    changed |= EnumCombo("動かし方", c.motion, kCameraShotMotionNames);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Truck: 開始位置から終了位置へ平行に動く\nOrbit: 注視点の周りを回り込む");

    // 位置は「今の画面」から取り込むのがいちばん早い。数値は微調整用
    const bool canCapture = context.world && context.world->HasResource<Camera *>();
    auto captureButton = [&](const char *id, bool toStart) {
        ImGui::SameLine();
        ImGui::BeginDisabled(!canCapture);
        if (ImGui::SmallButton(id)) changed |= CapturePose(context, c, toStart);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("今描かれているカメラの位置・向き・画角をここへ取り込む。\n"
                              "先に「止める」でプレビューを止めて、フリーカメラで視点を決めてから押す");
    };

    if (c.motion == CameraShotMotion::Truck)
    {
        changed |= ImGui::DragFloat3("カメラ 開始", &c.eyeStart.x, 0.05f);
        captureButton("今の画面##eyeS", true);
        changed |= ImGui::DragFloat3("カメラ 終了", &c.eyeEnd.x, 0.05f);
        captureButton("今の画面##eyeE", false);
    }
    else
    {
        changed |= ImGui::DragFloat("角度 開始(度)", &c.angleStart, 0.5f, -720.0f, 720.0f);
        captureButton("今の画面##orbS", true);
        changed |= ImGui::DragFloat("角度 終了(度)", &c.angleEnd, 0.5f, -720.0f, 720.0f);
        captureButton("今の画面##orbE", false);
        changed |= ImGui::DragFloat("距離 開始(m)", &c.radiusStart, 0.1f, 0.1f, 500.0f);
        changed |= ImGui::DragFloat("距離 終了(m)", &c.radiusEnd, 0.1f, 0.1f, 500.0f);
        changed |= ImGui::DragFloat("高さ 開始(m)", &c.heightStart, 0.1f, -100.0f, 300.0f);
        changed |= ImGui::DragFloat("高さ 終了(m)", &c.heightEnd, 0.1f, -100.0f, 300.0f);
    }
    changed |= ImGui::DragFloat3("見る所 開始", &c.lookStart.x, 0.05f);
    changed |= ImGui::DragFloat3("見る所 終了", &c.lookEnd.x, 0.05f);
    ImGui::TextDisabled("位置は対象からのずれ（+X=右, +Y=上, +Z=前）");

    if (ImGui::CollapsingHeader("詳細設定"))
    {
        changed |= EnumCombo("前のショットから", c.transitionIn, kCameraShotTransitionNames);
        if (c.transitionIn == CameraShotTransition::Blend)
            changed |= ImGui::DragFloat("ブレンド時間(秒)", &c.blendTime, 0.01f, 0.0f, 10.0f, "%.2f");
        changed |= ImGui::DragFloat("画角 開始(度)", &c.fovStart, 0.2f, 5.0f, 150.0f);
        changed |= ImGui::DragFloat("画角 終了(度)", &c.fovEnd, 0.2f, 5.0f, 150.0f);
        changed |= EnumCombo("動きのカーブ", c.ease, kTimelineEaseNames);
        changed |= RoleField("を見る（なし=映す対象）", c.lookSubject, true);
        changed |= EnumCombo("座標系", c.space, kCameraShotSpaceNames);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("SubjectLocal: 対象の向き基準。ショット開始時の向きで固定\nWorld: ワールド軸");
        changed |= ImGui::Checkbox("対象が動いたら追う", &c.followSubject);
        changed |= mi::InputText("メモ", c.comment);
    }
    return changed;
}

bool TimelineEditorWindow::DrawAnimation(AnimationClip &c)
{
    bool changed = false;
    changed |= ImGui::DragFloat("開始(秒)", &c.start, 0.01f, 0.0f, 120.0f, "%.2f");
    changed |= ImGui::DragFloat("長さ(秒)", &c.duration, 0.01f, 0.0f, 60.0f, "%.2f");
    changed |= RoleField("に再生させる", c.subject, false);

    bool pathChanged = false;
    ImGui::Text("クリップ: %s", c.sequence.empty() ? "(未設定)" : Stem(c.sequence).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("選ぶ...##seq"))
    {
        if (auto pathOpt = FileDialog::OpenPreset("AnimSequence"))
        {
            c.sequence  = NormalizePath(FileDialog::ConvertToRelativePath(*pathOpt));
            pathChanged = true;
        }
    }
    if (pathChanged)
    {
        // 長さをクリップに合わせ直す（前のクリップの長さのままにしない）
        c.duration = 0.0f;
        TimelineAssetLoader::ResolveAnimations(m_buffer);
        changed = true;
    }
    changed |= ImGui::DragFloat("再生速度", &c.speed, 0.01f, 0.05f, 5.0f, "%.2f");

    if (ImGui::CollapsingHeader("詳細設定"))
    {
        changed |= ImGui::Checkbox("ループ", &c.loop);
        changed |= ImGui::DragFloat("入りのブレンド(秒)", &c.blendIn, 0.01f, 0.0f, 2.0f, "%.2f");
        changed |= ImGui::DragFloat("戻りのブレンド(秒)", &c.blendOut, 0.01f, 0.0f, 2.0f, "%.2f");
        changed |= mi::InputText("クリップのパス", c.sequence);
        changed |= mi::InputText("メモ", c.comment);
        ImGui::TextDisabled("再生中は攻撃判定を出さない（音・エフェクトは出る）");
    }
    return changed;
}

bool TimelineEditorWindow::DrawEffect(EffectClip &c)
{
    bool changed = false;
    changed |= ImGui::DragFloat("時刻(秒)", &c.time, 0.01f, 0.0f, 120.0f, "%.2f");

    ImGui::Text("エフェクト: %s", c.effect.empty() ? "(未設定)" : Stem(c.effect).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("選ぶ...##vfx"))
    {
        if (auto pathOpt = FileDialog::OpenPreset("Prefab"))
        {
            // VFX は拡張子なしで指定する決まり（アニメのシーケンサーと同じ）
            c.effect = StripJsonExt(NormalizePath(FileDialog::ConvertToRelativePath(*pathOpt)));
            changed  = true;
        }
    }
    changed |= RoleField("の位置に出す", c.subject, true);
    changed |= ImGui::DragFloat3("ずらす量", &c.offset.x, 0.05f);
    changed |= ImGui::DragFloat("大きさ", &c.scale, 0.01f, 0.01f, 50.0f, "%.2f");

    if (ImGui::CollapsingHeader("詳細設定"))
    {
        changed |= ImGui::DragFloat("出し続ける長さ(秒)", &c.duration, 0.01f, 0.0f, 60.0f, "%.2f");
        changed |= ImGui::Checkbox("対象に付いていく", &c.follow);
        changed |= mi::InputText("武器のタグ", c.attachTag);
        changed |= ImGui::Checkbox("スキップしても出す", &c.fireOnSkip);
        changed |= mi::InputText("エフェクトのパス", c.effect);
        changed |= mi::InputText("メモ", c.comment);
        if (c.subject.empty())
            ImGui::TextDisabled("対象なし: ずらす量がワールド座標になる（大きさ・付いていくは効かない）");
    }
    return changed;
}

bool TimelineEditorWindow::DrawSound(EditorContext &context, SoundClip &c)
{
    bool changed = false;
    changed |= ImGui::DragFloat("時刻(秒)", &c.time, 0.01f, 0.0f, 120.0f, "%.2f");

    // 登録済みのイベント名から選ぶ（打ち間違うと黙って鳴らない）
    std::vector<std::string> names;
    if (context.world && context.world->HasResource<std::shared_ptr<IAudioAPI>>())
    {
        if (auto &api = context.world->GetResource<std::shared_ptr<IAudioAPI>>()) names = api->GetEventNames();
    }
    if (ImGui::BeginCombo("音", c.event.empty() ? "(選ぶ)" : c.event.c_str()))
    {
        for (const auto &n : names)
        {
            if (ImGui::Selectable(n.c_str(), n == c.event))
            {
                c.event = n;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    changed |= ImGui::DragFloat("音量", &c.volume, 0.01f, 0.0f, 4.0f, "%.2f");

    if (ImGui::CollapsingHeader("詳細設定"))
    {
        changed |= ImGui::DragFloat("ピッチ(オクターブ)", &c.pitch, 0.01f, -1.0f, 1.0f, "%.2f");
        changed |= ImGui::Checkbox("対象の位置から鳴らす（3D）", &c.spatial);
        if (c.spatial) changed |= RoleField("の位置", c.subject, false);
        changed |= ImGui::Checkbox("スキップしても鳴らす", &c.fireOnSkip);
        changed |= mi::InputText("音の名前", c.event);
        changed |= mi::InputText("メモ", c.comment);
    }
    return changed;
}

bool TimelineEditorWindow::DrawSignal(SignalClip &c)
{
    bool changed = false;
    changed |= ImGui::DragFloat("時刻(秒)", &c.time, 0.01f, 0.0f, 120.0f, "%.2f");

    // 種類を選ぶと、受け手（CinematicSignalSystem）がその引数をどう使うかに合わせた欄を出す
    if (ImGui::RadioButton("テロップ", c.name == "Telop")) { c.name = "Telop"; changed = true; }
    ImGui::SameLine();
    if (ImGui::RadioButton("BGM", c.name == "BGM")) { c.name = "BGM"; changed = true; }
    ImGui::SameLine();
    const bool isCustom = (c.name != "Telop" && c.name != "BGM");
    if (ImGui::RadioButton("その他", isCustom) && !isCustom) { c.name.clear(); changed = true; }

    if (c.name == "Telop")
    {
        changed |= mi::InputText("名前", c.param);
        changed |= mi::InputText("下の1行", c.param2);
        changed |= ImGui::DragFloat("表示する秒数", &c.value, 0.05f, 0.0f, 30.0f, "%.2f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 なら 3 秒");
    }
    else if (c.name == "BGM")
    {
        changed |= mi::InputText("曲（空で止める）", c.param);
    }
    else
    {
        changed |= mi::InputText("シグナル名", c.name);
        changed |= mi::InputText("param", c.param);
        changed |= mi::InputText("param2", c.param2);
        changed |= ImGui::DragFloat("value", &c.value, 0.05f);
    }

    if (ImGui::CollapsingHeader("詳細設定"))
    {
        changed |= ImGui::Checkbox("スキップしても出す", &c.fireOnSkip);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("BGM や戦闘開始のように、飛ばすと本編が壊れるものは ON。テロップは OFF");
        changed |= mi::InputText("メモ", c.comment);
    }
    return changed;
}

// ============================================================
// カメラ位置の取り込み
// ============================================================

bool TimelineEditorWindow::ResolveRole(EditorContext &context, const std::string &role, Vector3 &pos,
                                       float &yaw) const
{
    if (role.empty())
    {
        pos = Vector3::Zero;
        yaw = 0.0f;
        return true;
    }

    // 役割の結び付けはプレイヤーが持っている（ゲーム側の TimelineRoleBindSystem が書く）
    const auto *player = context.world->GetComponent<TimelinePlayerComponent>(m_player);
    if (!player) return false;
    const auto it = player->bindings.find(role);
    if (it == player->bindings.end() || !context.world->IsEntityValid(it->second)) return false;

    const auto *trans = context.world->GetComponent<TransformComponent>(it->second);
    if (!trans) return false;

    pos            = trans->GetRenderWorldPosition();
    const auto &m  = trans->renderWorldMatrix;
    const float fx = m._31;
    const float fz = m._33;
    yaw            = (fx * fx + fz * fz > 1e-8f) ? std::atan2(fx, fz) : 0.0f;
    return true;
}

bool TimelineEditorWindow::CapturePose(EditorContext &context, CameraShotClip &shot, bool toStart) const
{
    if (!context.world || !context.world->HasResource<Camera *>()) return false;
    const Camera *cam = context.world->GetResource<Camera *>();
    if (!cam) return false;

    const std::string &lookRole = shot.lookSubject.empty() ? shot.subject : shot.lookSubject;

    Vector3 subjectPos, lookPos;
    float   yaw = 0.0f, lookYaw = 0.0f;
    if (!ResolveRole(context, shot.subject, subjectPos, yaw) || !ResolveRole(context, lookRole, lookPos, lookYaw))
    {
        CCL_LOG_WARN(LogCategory::Editor,
                     "[TimelineEditor] 役割が結び付いていないので取り込めません（ゲームを動かして役割が揃ってから）");
        return false;
    }
    if (shot.space == CameraShotSpace::World) yaw = 0.0f;

    // ワールド → 対象の座標系。再生時は CreateRotationY(yaw) で回すので、その逆を掛ける
    const Matrix toLocal = Matrix::CreateRotationY(-yaw);
    const Matrix toWorld = Matrix::CreateRotationY(yaw);

    const Vector3 eye   = cam->GetEye();
    Vector3       front = cam->GetFront();
    front.Normalize();

    // 注視点は「今の注視の目標」に一番近い、視線上の点にする。視線の方向は画面どおりに保ちつつ、
    // 注視点の距離（＝回り込みの中心）を元の設定から大きく変えないため
    const Vector3 lookOffset = toStart ? shot.lookStart : shot.lookEnd;
    const Vector3 lookTarget = lookPos + Vector3::TransformNormal(lookOffset, toWorld);
    const float   along      = (std::max)((lookTarget - eye).Dot(front), 1.0f);
    const Vector3 lookPoint  = eye + front * along;

    (toStart ? shot.lookStart : shot.lookEnd) = Vector3::TransformNormal(lookPoint - lookPos, toLocal);

    if (shot.motion == CameraShotMotion::Truck)
    {
        (toStart ? shot.eyeStart : shot.eyeEnd) = Vector3::TransformNormal(eye - subjectPos, toLocal);
    }
    else
    {
        // Orbit は注視点を中心にした角度・距離・高さ。規約は TPS カメラと同じ（X軸=cos, Z軸=sin）
        const Vector3 rel = Vector3::TransformNormal(eye - lookPoint, toLocal);
        (toStart ? shot.angleStart : shot.angleEnd)   = XMConvertToDegrees(std::atan2(rel.z, rel.x));
        (toStart ? shot.radiusStart : shot.radiusEnd) = std::sqrt(rel.x * rel.x + rel.z * rel.z);
        (toStart ? shot.heightStart : shot.heightEnd) = rel.y;
    }

    (toStart ? shot.fovStart : shot.fovEnd) = XMConvertToDegrees(cam->GetFovY());
    return true;
}
