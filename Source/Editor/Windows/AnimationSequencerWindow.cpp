// ===================================================================================
// ファイル: AnimationSequencerWindow.cpp
// 役割       : AnimSequence 編集ウィンドウの実装。
// 責務       : ImSequencer/ImCurveEdit の仲介(pImpl)、UI構築、JSON保存/ロード、
//              選択エンティティへのリアルタイムプレビュー。
// 設計方針   : イベント種別は AnimEventType(enum)で型安全に分岐。IOは FileSystem 経由。
//              状態はインスタンスメンバのみ(static禁止)。
// ===================================================================================
#include "AnimationSequencerWindow.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Animation/Systems/AnimationSystem.h"
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Graphics/Resource/ModelResource.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/Combat/CombatRosterComponent.h"

#include "Engine/Platform/Core/Dialog.h"     // Engine::FileDialog
#include "Engine/Platform/Core/FileSystem.h" // FileSystem
#include "Engine/Platform/Core/Logger.h"

#include "ImSequencer.h"
#include <ImCurveEdit.h>
#include <imgui.h>
#include <json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
/// @brief 秒⇔フレーム変換の基準FPS。マジック60を1箇所に集約。
constexpr float kEditorFps = 60.0f;

inline int SecToFrame(float sec)
{
    return static_cast<int>(sec * kEditorFps);
}
inline float FrameToSec(int frame)
{
    return static_cast<float>(frame) / kEditorFps;
}
} // namespace

// ===================================================================================
// イベント種別の定義テーブル（マジックintを排除し、1箇所に集約）
//   種別追加時はこのテーブルに1行足すだけ。色・名前・デフォルト長を一元管理する。
// ===================================================================================
namespace
{
struct EventTypeDesc
{
    AnimEventType type;
    const char   *name;
    unsigned int  color;      ///< ImSequencer ブロック色 (ABGR)
    int           defaultLen; ///< 追加時のデフォルト区間長(フレーム)
};

// AnimEventType の順序と一致させること（index = (int)type）
constexpr EventTypeDesc kEventTypes[] = {
    {AnimEventType::HitBox, "HitBox (Attack)", 0xFF5050D0, 30},
    {AnimEventType::Sound, "Sound (SE)", 0xFFD08050, 10},
    {AnimEventType::Effect, "Effect (Effekseer)", 0xFF50D050, 10},
    {AnimEventType::Blackboard, "Blackboard (Flag)", 0xFF50A0D0, 20},
    {AnimEventType::SpawnHitbox, "Spawn HitBox (Spatial)", 0xFFD050A0, 30},
    {AnimEventType::Trail, "Trail (Weapon)", 0xFFD0D030, 30},
};

constexpr int kEventTypeCount = (int)AnimEventType::Count;

const EventTypeDesc &DescOf(AnimEventType t)
{
    int i = (int)t;
    if (i < 0 || i >= kEventTypeCount)
        i = 0;
    return kEventTypes[i];
}
} // namespace

// ===================================================================================
// AnimationSequenceImpl : ImSequencer <-> AnimSequence の仲介(pImpl)
//   cache(UIブロック) と targetSequence(ECSデータ) を相互変換する。
//   イベントの全パラメータは AnimNotifyEvent に直接対応する(多目的float流用なし)。
// ===================================================================================
class AnimationSequenceImpl : public ImSequencer::SequenceInterface
{
  public:
    AnimSequence *targetSequence = nullptr;

    int mFrameMin = 0;
    int mFrameMax = (int)kEditorFps * 5;

    /// @brief 台本(events) を UI が直接編集できるよう、ブロックは events の参照ではなく
    ///        events 自体を直接編集する設計にする(中間cache廃止で同期バグを根絶)。
    /// @note  ImSequencer は frameStart/End への int* を要求するため、events の startTime/
    ///        endTime(秒) とは別に、フレーム値の作業配列を持つ。
    struct FrameSlot
    {
        int start;
        int end;
    };
    std::vector<FrameSlot> frames; ///< events と同インデックスのフレーム値

    /// @brief events からフレーム作業配列を再構築する。
    void RebuildFrames()
    {
        frames.clear();
        if (!targetSequence)
            return;
        frames.reserve(targetSequence->events.size());
        for (const auto &e : targetSequence->events)
            frames.push_back({SecToFrame(e.startTime), SecToFrame(e.endTime)});
    }

    /// @brief フレーム作業配列を events(秒) に書き戻し、startTime 昇順でソートする。
    void CommitFrames()
    {
        if (!targetSequence)
            return;
        const size_t n = (std::min)(frames.size(), targetSequence->events.size());
        for (size_t i = 0; i < n; ++i)
        {
            targetSequence->events[i].startTime = FrameToSec(frames[i].start);
            targetSequence->events[i].endTime   = FrameToSec(frames[i].end);
        }
        SortAndResync();
    }

    /// @brief events を startTime 昇順にソートし、フレーム配列を再構築する。
    void SortAndResync()
    {
        if (!targetSequence)
            return;
        std::sort(targetSequence->events.begin(), targetSequence->events.end(),
            [](const AnimNotifyEvent &a, const AnimNotifyEvent &b) {
                return a.startTime < b.startTime;
            });
        RebuildFrames();
    }

    void UpdateDuration(float durationSeconds)
    {
        mFrameMax = (int)(durationSeconds * kEditorFps * 1.1f);
        if (mFrameMax < (int)kEditorFps)
            mFrameMax = (int)kEditorFps;
    }

    // ---- ImSequencer::SequenceInterface ----
    int GetFrameMin() const override
    {
        return mFrameMin;
    }
    int GetFrameMax() const override
    {
        return mFrameMax;
    }
    int GetItemCount() const override
    {
        return targetSequence ? (int)targetSequence->events.size() : 0;
    }
    int GetItemTypeCount() const override
    {
        return kEventTypeCount;
    }

    const char *GetItemTypeName(int typeIndex) const override
    {
        if (typeIndex < 0 || typeIndex >= kEventTypeCount)
            return "Unknown";
        return kEventTypes[typeIndex].name;
    }

    const char *GetItemLabel(int index) const override
    {
        static char temp[160];
        if (!targetSequence || index < 0 || index >= (int)targetSequence->events.size())
            return "";
        const auto &e = targetSequence->events[index];
        const char *p = e.stringParam.empty() ? "Empty" : e.stringParam.c_str();
        switch (e.type)
        {
        case AnimEventType::HitBox:
            sprintf_s(temp, "[%d] Attack: %s", index, p);
            break;
        case AnimEventType::Sound:
            sprintf_s(temp, "[%d] Sound: %s", index, p);
            break;
        case AnimEventType::Effect:
            sprintf_s(temp, "[%d] Effect: %s", index, p);
            break;
        case AnimEventType::Blackboard:
            sprintf_s(temp, "[%d] BB: %s", index, p);
            break;
        case AnimEventType::SpawnHitbox:
            sprintf_s(temp, "[%d] Spawn R=%.1f", index, e.radius);
            break;
        case AnimEventType::Trail:
            sprintf_s(temp, "[%d] Trail: %s", index, p);
            break;
        default:
            sprintf_s(temp, "[%d] Event", index);
            break;
        }
        return temp;
    }

    void Get(int index, int **start, int **end, int *type, unsigned int *color) override
    {
        if (!targetSequence || index < 0 || index >= (int)targetSequence->events.size())
            return;
        // frames が events と同サイズであることを保証
        if ((int)frames.size() != (int)targetSequence->events.size())
            RebuildFrames();

        if (start)
            *start = &frames[index].start;
        if (end)
            *end = &frames[index].end;
        if (type)
            *type = (int)targetSequence->events[index].type;
        if (color)
            *color = DescOf(targetSequence->events[index].type).color;
    }

    void Add(int type) override
    {
        if (!targetSequence)
            return;
        if (type < 0 || type >= kEventTypeCount)
            type = 0;

        AnimNotifyEvent e;
        e.type      = (AnimEventType)type;
        e.startTime = 0.0f;
        e.endTime   = FrameToSec(kEventTypes[type].defaultLen);
        if (e.type == AnimEventType::Effect)
            e.effectScale = 1.0f;
        targetSequence->events.push_back(e);
        RebuildFrames();
    }

    void Del(int index) override
    {
        if (!targetSequence || index < 0 || index >= (int)targetSequence->events.size())
            return;
        targetSequence->events.erase(targetSequence->events.begin() + index);
        RebuildFrames();
    }

    void Duplicate(int index) override
    {
        if (!targetSequence || index < 0 || index >= (int)targetSequence->events.size())
            return;
        targetSequence->events.push_back(targetSequence->events[index]);
        RebuildFrames();
    }

    size_t GetCustomHeight(int) override
    {
        return 0;
    }
    void DoubleClick(int) override
    {
    }
};

// ===================================================================================
// SequencerCurveDelegate : AnimSequence のカーブを ImCurveEdit に橋渡し
// ===================================================================================
struct SequencerCurveDelegate : public ImCurveEdit::Delegate
{
    AnimSequence *targetSeq = nullptr;
    ImVec2        points[2][256];
    bool          visible[2]   = {true, true};
    ImVec2        m_min        = ImVec2(0.0f, -1.0f);
    ImVec2        m_max        = ImVec2(1.0f, 3.0f);
    float         animDuration = 1.0f;

    void SetSequence(AnimSequence *seq)
    {
        targetSeq = seq;
        if (targetSeq)
            animDuration = targetSeq->duration;
        UpdatePoints();
    }

    void FitViewToDuration()
    {
        if (!targetSeq)
            return;
        m_min.x = 0.0f;
        m_max.x = (targetSeq->duration > 0.0f) ? targetSeq->duration : 1.0f;
    }

    void UpdatePoints()
    {
        if (!targetSeq)
            return;
        for (size_t i = 0; i < targetSeq->speedCurve.keys.size() && i < 256; ++i)
            points[0][i] = ImVec2(targetSeq->speedCurve.keys[i].time,
                targetSeq->speedCurve.keys[i].value);
        for (size_t i = 0; i < targetSeq->rootMotionCurve.keys.size() && i < 256; ++i)
            points[1][i] = ImVec2(targetSeq->rootMotionCurve.keys[i].time,
                targetSeq->rootMotionCurve.keys[i].value);
    }

    size_t GetCurveCount() override
    {
        return 2;
    }
    size_t GetPointCount(size_t ci) override
    {
        if (!targetSeq)
            return 0;
        return (ci == 0) ? targetSeq->speedCurve.keys.size()
                         : targetSeq->rootMotionCurve.keys.size();
    }
    ImCurveEdit::CurveType GetCurveType(size_t) const override
    {
        return ImCurveEdit::CurveSmooth;
    }
    bool IsVisible(size_t ci) override
    {
        return visible[ci];
    }
    uint32_t GetCurveColor(size_t ci) override
    {
        return ci == 0 ? 0xFF00FF00 : 0xFF00A5FF;
    }
    ImVec2 *GetPoints(size_t ci) override
    {
        return points[ci];
    }
    ImVec2 &GetMin() override
    {
        return m_min;
    }
    ImVec2 &GetMax() override
    {
        return m_max;
    }
    unsigned int GetBackgroundColor() override
    {
        return 0x00000000;
    }

    int EditPoint(size_t ci, int pi, ImVec2 value) override
    {
        if (!targetSeq)
            return pi;
        value.x           = (std::max)(0.0f, value.x);
        AnimationCurve &c = (ci == 0) ? targetSeq->speedCurve : targetSeq->rootMotionCurve;
        if (pi >= 0 && pi < (int)c.keys.size())
        {
            c.keys[pi].time  = value.x;
            c.keys[pi].value = value.y;
        }
        UpdatePoints();
        return pi;
    }

    void AddPoint(size_t ci, ImVec2 value) override
    {
        if (!targetSeq)
            return;
        AnimationCurve &c = (ci == 0) ? targetSeq->speedCurve : targetSeq->rootMotionCurve;
        c.keys.push_back({value.x, value.y});
        std::sort(c.keys.begin(), c.keys.end(),
            [](const CurveKey &a, const CurveKey &b) { return a.time < b.time; });
        UpdatePoints();
    }
};

// ===================================================================================
// グリッド + ルーラー オーバーレイ
// ===================================================================================
static void DrawCurveEditorOverlay(ImDrawList *dl, ImVec2 pos, ImVec2 size,
    ImVec2 vMin, ImVec2 vMax, float duration)
{
    if (size.x <= 0 || size.y <= 0)
        return;
    if (vMax.x <= vMin.x || vMax.y <= vMin.y)
        return;

    const float left = pos.x, top = pos.y;
    const float right = left + size.x, bottom = top + size.y;
    const float PAD = 6.0f;
    dl->PushClipRect(pos, ImVec2(right, bottom), true);

    const ImU32 cMinor = IM_COL32(70, 70, 70, 110);
    const ImU32 cMajor = IM_COL32(100, 100, 100, 160);
    const ImU32 cZero  = IM_COL32(180, 180, 180, 200);
    const ImU32 cDur   = IM_COL32(255, 180, 60, 200);
    const ImU32 cLabel = IM_COL32(210, 210, 210, 230);
    const ImU32 cShd   = IM_COL32(0, 0, 0, 200);

    auto sx = [&](float t) { return left + PAD + (t - vMin.x) / (vMax.x - vMin.x) * (size.x - 2 * PAD); };
    auto sy = [&](float v) { return bottom - PAD - (v - vMin.y) / (vMax.y - vMin.y) * (size.y - 2 * PAD); };

    float xr = vMax.x - vMin.x, xstep = 0.1f;
    if (xr > 5.0f)
        xstep = 0.5f;
    if (xr > 20.0f)
        xstep = 1.0f;
    for (float t = std::ceil(vMin.x / xstep) * xstep; t <= vMax.x; t += xstep)
    {
        float x = sx(t);
        if (x < left || x > right)
            continue;
        bool major = (std::fmod(std::abs(t), xstep * 5.0f) < xstep * 0.5f);
        dl->AddLine(ImVec2(x, top), ImVec2(x, bottom), major ? cMajor : cMinor, major ? 1.0f : 0.5f);
        if (major)
        {
            char l[16];
            snprintf(l, sizeof(l), "%.2fs", t);
            dl->AddText(ImVec2(x + 3, top + 3), cShd, l);
            dl->AddText(ImVec2(x + 2, top + 2), cLabel, l);
        }
    }

    float yr = vMax.y - vMin.y, ystep = 0.5f;
    if (yr > 5.0f)
        ystep = 1.0f;
    for (float v = std::ceil(vMin.y / ystep) * ystep; v <= vMax.y; v += ystep)
    {
        float y = sy(v);
        if (y < top || y > bottom)
            continue;
        bool major = (std::fmod(std::abs(v), ystep * 2.0f) < ystep * 0.5f);
        dl->AddLine(ImVec2(left, y), ImVec2(right, y), major ? cMajor : cMinor, major ? 1.0f : 0.5f);
        if (major)
        {
            char l[16];
            snprintf(l, sizeof(l), "%.1f", v);
            dl->AddText(ImVec2(left + PAD + 2, y - 9), cShd, l);
            dl->AddText(ImVec2(left + PAD + 1, y - 10), cLabel, l);
        }
    }

    if (vMin.y <= 0.0f && 0.0f <= vMax.y)
        dl->AddLine(ImVec2(left, sy(0.0f)), ImVec2(right, sy(0.0f)), cZero, 1.5f);

    if (duration > vMin.x && duration < vMax.x)
    {
        float x = sx(duration);
        dl->AddLine(ImVec2(x, top), ImVec2(x, bottom), cDur, 2.0f);
        dl->AddText(ImVec2(x + 3, bottom - 18), cDur, "END");
    }
    dl->PopClipRect();
}

// ===================================================================================
// AnimationSequencerWindow 実装
// ===================================================================================
AnimationSequencerWindow::AnimationSequencerWindow()
    : EditorWindow("Sequencer")
{
    _sequencerImpl = std::make_unique<AnimationSequenceImpl>();
    _curveDelegate = std::make_unique<SequencerCurveDelegate>();

    _sequencerImpl->targetSequence = &_editingSequence;
    _sequencerImpl->UpdateDuration(_editingSequence.duration);
    _sequencerImpl->RebuildFrames();
    SetVisible(true);
}

AnimationSequencerWindow::~AnimationSequencerWindow() = default;

void AnimationSequencerWindow::DrawContents(EditorContext &context)
{
    if (context.selectedEntity == CCL::ECS::InvalidEntityID || context.selectedEntity == 0)
    {
        ImGui::TextDisabled("Select an entity to edit animation.");
        return;
    }

    const CCL::ECS::EntityID entity = context.selectedEntity;

    auto *animator  = context.world->GetComponent<AnimatorComponent>(entity);
    auto *modelComp = context.world->GetComponent<ModelComponent>(entity);
    if (!animator || !modelComp || !modelComp->GetModel())
    {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Entity needs Animator and Model.");
        return;
    }

    Model         *model    = modelComp->GetModel();
    ModelResource *resource = model->GetResourceMutable();
    if (!resource)
        return;
    auto &animations = resource->GetAnimationsMutable();
    if (animations.empty())
    {
        ImGui::TextDisabled("Model has no animations.");
        return;
    }

    // ---- エンティティ切替検出：状態を完全リセット ----
    if (entity != _lastSelectedEntity)
    {
        if (context.isAnimEditMode)
        {
            animator->isEditorOverride = false;
            if (_savedSequence)
                animator->currentSequence = _savedSequence;
            context.isAnimEditMode = false;
        }
        _editingSequence = AnimSequence{};
        _savePath.clear();
        _selectedAnimIndex     = -1;
        _prevSelectedAnimIndex = -2;
        _isPlaying             = false;
        _wasEditMode           = false;
        _savedSequence         = nullptr;
        _selectedEntry         = -1;
        _firstFrame            = 0;
        _fitCurveView          = true;

        _sequencerImpl->targetSequence = &_editingSequence;
        _sequencerImpl->UpdateDuration(_editingSequence.duration);
        _sequencerImpl->RebuildFrames();
        _lastSelectedEntity = entity;
    }

    // ====================================================
    // ツールバー: Save / Load
    // ====================================================
    if (ImGui::Button("Save JSON..."))
    {
        if (auto path = Engine::FileDialog::SavePreset("AnimSequence", "Save Anim Sequence"))
        {
            _savePath                     = Engine::FileDialog::ConvertToRelativePath(*path);
            _editingSequence.sequenceName = std::filesystem::path(_savePath).stem().string();
            _sequencerImpl->SortAndResync();

            nlohmann::json j = _editingSequence;
            if (FileSystem::WriteAllText(_savePath, j.dump(4)))
                CCL_LOG_SUCCESS(LogCategory::Editor, "Sequence Saved: %s", _savePath.c_str());
            else
                CCL_LOG_ERROR(LogCategory::Editor, "Sequence Save Failed: %s", _savePath.c_str());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Load JSON..."))
    {
        if (auto path = Engine::FileDialog::OpenPreset("AnimSequence", "Load Anim Sequence"))
        {
            std::string loadPath = Engine::FileDialog::ConvertToRelativePath(*path);
            std::string text     = FileSystem::ReadAllText(loadPath);
            if (!text.empty())
            {
                try
                {
                    nlohmann::json j = nlohmann::json::parse(text);
                    _editingSequence = j.get<AnimSequence>();
                    _savePath        = loadPath;

                    // ターゲットアニメをコンボに同期
                    _selectedAnimIndex = 0;
                    for (int i = 0; i < (int)animations.size(); ++i)
                        if (animations[i].name == _editingSequence.targetAnimName)
                        {
                            _selectedAnimIndex = i;
                            break;
                        }
                    _prevSelectedAnimIndex = -2;

                    _sequencerImpl->targetSequence = &_editingSequence;
                    _sequencerImpl->UpdateDuration(_editingSequence.duration);
                    _sequencerImpl->SortAndResync();
                    _selectedEntry = -1;
                    _fitCurveView  = true;
                    CCL_LOG_SUCCESS(LogCategory::Editor, "Sequence Loaded: %s", loadPath.c_str());
                }
                catch (const std::exception &e)
                {
                    CCL_LOG_ERROR(LogCategory::Editor, "Load failed: %s", e.what());
                }
            }
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("File: %s", _savePath.empty() ? "None" : _savePath.c_str());

    // シーケンス名
    char nameBuf[128];
    strcpy_s(nameBuf, sizeof(nameBuf), _editingSequence.sequenceName.c_str());
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputText("Sequence Name", nameBuf, sizeof(nameBuf)))
        _editingSequence.sequenceName = nameBuf;

    // ターゲットアニメ
    ImGui::SameLine();
    if (_selectedAnimIndex < 0 || _selectedAnimIndex >= (int)animations.size())
        _selectedAnimIndex = 0;
    ImGui::SetNextItemWidth(250);
    if (ImGui::BeginCombo("Target Animation", animations[_selectedAnimIndex].name.c_str()))
    {
        for (int i = 0; i < (int)animations.size(); ++i)
        {
            bool sel = (_selectedAnimIndex == i);
            if (ImGui::Selectable(animations[i].name.c_str(), sel))
                _selectedAnimIndex = i;
            if (sel)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (_prevSelectedAnimIndex != _selectedAnimIndex)
    {
        _prevSelectedAnimIndex          = _selectedAnimIndex;
        _editingSequence.targetAnimName = animations[_selectedAnimIndex].name;
        _editingSequence.duration       = animations[_selectedAnimIndex].secondsLength;
        _sequencerImpl->UpdateDuration(_editingSequence.duration);
        _fitCurveView = true;
    }

    // ====================================================
    // 編集モード制御（currentSequence 退避/復元）
    // ====================================================
    if (context.isAnimEditMode)
    {
        if (!_wasEditMode)
        {
            _savedSequence       = animator->currentSequence; // ★退避
            _wasEditMode         = true;
            context.animEditTime = animator->currentTimer;
        }
        animator->isEditorOverride = true;
        animator->currentSequence  = &_editingSequence;
    }
    else if (_wasEditMode)
    {
        animator->currentSequence  = _savedSequence; // ★復元
        animator->isEditorOverride = false;
        _savedSequence             = nullptr;
        _wasEditMode               = false;
        _isPlaying                 = false;
    }

    ImGui::Separator();

    // ====================================================
    // 2ペインレイアウト
    // ====================================================
    if (!ImGui::BeginTable("SeqLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
        return;
    ImGui::TableSetupColumn("Timeline", ImGuiTableColumnFlags_WidthStretch, 0.7f);
    ImGui::TableSetupColumn("Inspector", ImGuiTableColumnFlags_WidthStretch, 0.3f);
    ImGui::TableNextRow();

    // ---- 左: タイムライン ----
    ImGui::TableSetColumnIndex(0);

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Space))
        _isPlaying = !_isPlaying;

    if (context.isAnimEditMode)
    {
        if (_isPlaying)
        {
            context.animEditTime += ImGui::GetIO().DeltaTime * animator->playbackSpeed;
            if (_editingSequence.duration > 0.0f && context.animEditTime > _editingSequence.duration)
                context.animEditTime = animator->isLoop
                                           ? std::fmod(context.animEditTime, _editingSequence.duration)
                                           : _editingSequence.duration;
            animator->currentTimer = context.animEditTime;
        }
        if (ImGui::Button(_isPlaying ? "|| Pause" : "> Play"))
            _isPlaying = !_isPlaying;
        ImGui::SameLine();
    }
    else
        _isPlaying = false;

    // イベント追加（現在時刻に配置して選択）
    auto AddEvt = [&](AnimEventType t) {
        _sequencerImpl->Add((int)t);
        auto &e     = _editingSequence.events.back();
        int   s     = context.isAnimEditMode ? SecToFrame(context.animEditTime) : 0;
        e.startTime = FrameToSec(s);
        e.endTime   = FrameToSec(s + DescOf(t).defaultLen);
        _sequencerImpl->SortAndResync();
        _selectedEntry = (int)_editingSequence.events.size() - 1;
    };

    if (ImGui::Button("+ Attack"))
        AddEvt(AnimEventType::HitBox);
    ImGui::SameLine();
    if (ImGui::Button("+ Spawn"))
        AddEvt(AnimEventType::SpawnHitbox);
    ImGui::SameLine();
    if (ImGui::Button("+ Sound"))
        AddEvt(AnimEventType::Sound);
    ImGui::SameLine();
    if (ImGui::Button("+ Effect"))
        AddEvt(AnimEventType::Effect);
    ImGui::SameLine();
    if (ImGui::Button("+ BB Flag"))
        AddEvt(AnimEventType::Blackboard);

    int curFrame  = context.isAnimEditMode ? SecToFrame(context.animEditTime)
                                           : SecToFrame(animator->currentTimer);
    int prevFrame = curFrame;

    _expanded    = true;
    bool changed = ImSequencer::Sequencer(
        _sequencerImpl.get(), &curFrame, &_expanded, &_selectedEntry, &_firstFrame,
        ImSequencer::SEQUENCER_EDIT_STARTEND | ImSequencer::SEQUENCER_ADD |
            ImSequencer::SEQUENCER_DEL | ImSequencer::SEQUENCER_CHANGE_FRAME);

    if (changed)
        _sequencerImpl->CommitFrames();

    // Delete キーで削除
    if (_selectedEntry >= 0 &&
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        ImGui::IsKeyPressed(ImGuiKey_Delete))
    {
        _sequencerImpl->Del(_selectedEntry);
        _selectedEntry = -1;
    }

    // 矢印キーでナッジ
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            _selectedEntry = -1;
        int nudge = ImGui::GetIO().KeyShift ? 5 : 1;
        if (_selectedEntry >= 0 && _selectedEntry < (int)_sequencerImpl->frames.size())
        {
            auto &f     = _sequencerImpl->frames[_selectedEntry];
            bool  moved = false;
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
            {
                f.start -= nudge;
                f.end -= nudge;
                moved = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
            {
                f.start += nudge;
                f.end += nudge;
                moved = true;
            }
            if (moved)
            {
                if (f.start < 0)
                {
                    f.end += -f.start;
                    f.start = 0;
                }
                _sequencerImpl->CommitFrames();
            }
        }
    }

    if (curFrame != prevFrame)
    {
        _isPlaying             = false;
        context.animEditTime   = FrameToSec(curFrame);
        animator->currentTimer = context.animEditTime;
    }

    // プレビュー反映
    if (context.isAnimEditMode && context.systemManager)
    {
        if (auto *animSys = context.systemManager->GetSystem<AnimationSystem>())
        {
            animSys->UpdateManual(entity, context.animEditTime);
            if (auto *tr = context.world->GetComponent<TransformComponent>(entity))
                model->UpdateTransform(tr->worldMatrix);
        }
    }

    // ---- 右: インスペクタ + カーブ ----
    ImGui::TableSetColumnIndex(1);
    ImGui::Text("Duration: %.2fs", _editingSequence.duration);
    ImGui::Checkbox("Edit Mode (Preview)", &context.isAnimEditMode);
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.5f, 1, 0.5f, 1), "Selected Event");
    ImGui::Spacing();

    if (_selectedEntry >= 0 && _selectedEntry < (int)_editingSequence.events.size())
    {
        auto &e  = _editingSequence.events[_selectedEntry];
        bool  ch = false;

        ImGui::TextDisabled("Type: %s", DescOf(e.type).name);
        ImGui::Text("Time: %.2fs - %.2fs", e.startTime, e.endTime);
        ImGui::Spacing();

        // stringParam 編集用バッファ
        char buf[256];
        strcpy_s(buf, sizeof(buf), e.stringParam.c_str());

        switch (e.type)
        {
        case AnimEventType::HitBox: {
            // 武器タグ（Roster があればコンボ、無ければ手入力）
            auto *roster = context.world->GetComponent<CombatRosterComponent>(entity);
            if (roster && roster->count > 0)
            {
                ImGui::Text("Weapon Tag:");
                ImGui::SetNextItemWidth(-1);
                const char *preview = e.stringParam.empty() ? "Select Tag..." : e.stringParam.c_str();
                if (ImGui::BeginCombo("##Tag", preview))
                {
                    for (int i = 0; i < roster->count; ++i)
                    {
                        bool sel = (e.stringParam == roster->entries[i].tag);
                        if (ImGui::Selectable(roster->entries[i].tag, sel))
                        {
                            e.stringParam = roster->entries[i].tag;
                            ch            = true;
                        }
                        if (sel)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }
            else
            {
                ImGui::Text("Weapon Tag (no roster):");
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputText("##Tag", buf, sizeof(buf)))
                {
                    e.stringParam = buf;
                    ch            = true;
                }
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1, 0.5f, 0.5f, 1), "Attack");
            if (ImGui::DragFloat("Damage", &e.damage, 0.5f, 0.0f, 1000.0f, "%.1f"))
                ch = true;
            ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "Hit Stop");
            if (ImGui::SliderFloat("Duration", &e.hitStopDuration, 0.0f, 1.0f, "%.2f s"))
                ch = true;
            if (ImGui::SliderFloat("TimeScale", &e.hitStopScale, 0.0f, 1.0f, "%.2f x"))
                ch = true;
            break;
        }
        case AnimEventType::SpawnHitbox:
            ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "Spatial Hitbox");
            if (ImGui::DragFloat3("Offset", &e.offset.x, 0.05f))
                ch = true;
            if (ImGui::DragFloat("Radius", &e.radius, 0.05f, 0.0f, 100.0f))
                ch = true;
            if (ImGui::DragFloat("Damage", &e.damage, 0.5f, 0.0f, 1000.0f, "%.1f"))
                ch = true;
            if (ImGui::SliderFloat("HitStop", &e.hitStopDuration, 0.0f, 1.0f, "%.2f s"))
                ch = true;
            break;

        case AnimEventType::Sound:
            ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "FMOD Event Path");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##Snd", buf, sizeof(buf)))
            {
                e.stringParam = buf;
                ch            = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("event:/SFX/SwordSwing");
            break;

        case AnimEventType::Effect:
            ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "Effekseer Path");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##Efk", buf, sizeof(buf)))
            {
                e.stringParam = buf;
                ch            = true;
            }
            if (ImGui::DragFloat3("Offset", &e.offset.x, 0.05f))
                ch = true;
            if (ImGui::DragFloat("Scale", &e.effectScale, 0.05f, 0.0f, 100.0f, "%.2f x"))
                ch = true;
            if (ImGui::Checkbox("Follow Parent", &e.effectFollow))
                ch = true;
            break;

        case AnimEventType::Blackboard:
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "Blackboard Flag");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##BB", buf, sizeof(buf)))
            {
                e.stringParam = buf;
                ch            = true;
            }
            ImGui::TextDisabled("Active during [%.2fs - %.2fs]", e.startTime, e.endTime);
            break;

        case AnimEventType::Trail:
            // Trail 専用UIは現状なし（TrailComponent未実装）。区間のみ編集可。
            ImGui::TextDisabled("Trail (no component yet). Interval only.");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##Trail", buf, sizeof(buf)))
            {
                e.stringParam = buf;
                ch            = true;
            }
            break;

        default:
            break;
        }

        if (ch)
            _sequencerImpl->SortAndResync();
    }
    else
    {
        ImGui::TextDisabled("No event selected.\nClick a block on the timeline.");
    }

    // ---- カーブエディタ ----
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(1, 0.6f, 0.2f, 1), "Animation Curves");

    ImGui::SetNextItemWidth(130);
    float yR[2] = {_curveDelegate->m_min.y, _curveDelegate->m_max.y};
    if (ImGui::DragFloat2("Y-Range", yR, 0.05f))
    {
        _curveDelegate->m_min.y = yR[0];
        _curveDelegate->m_max.y = yR[1];
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130);
    float xR[2] = {_curveDelegate->m_min.x, _curveDelegate->m_max.x};
    if (ImGui::DragFloat2("X-Range", xR, 0.01f, 0.0f, 100.0f))
        if (xR[1] > xR[0] + 0.01f)
        {
            _curveDelegate->m_min.x = xR[0];
            _curveDelegate->m_max.x = xR[1];
        }
    ImGui::SameLine();
    if (ImGui::Button("Fit"))
        _fitCurveView = true;

    ImGui::Checkbox("Speed", &_curveDelegate->visible[0]);
    ImGui::SameLine();
    ImGui::Checkbox("RootMotion", &_curveDelegate->visible[1]);
    ImGui::SameLine();
    ImGui::TextDisabled("[Scroll:Pan | Ctrl+Scroll:Zoom]");

    if (_fitCurveView && _curveDelegate->targetSeq)
    {
        _curveDelegate->FitViewToDuration();
        _fitCurveView = false;
    }
    _curveDelegate->SetSequence(&_editingSequence);

    ImVec2 cPos  = ImGui::GetCursorScreenPos();
    ImVec2 cSize = ImVec2(ImGui::GetContentRegionAvail().x, 260.0f);
    DrawCurveEditorOverlay(ImGui::GetWindowDrawList(), cPos, cSize,
        _curveDelegate->m_min, _curveDelegate->m_max, _curveDelegate->animDuration);
    ImCurveEdit::Edit(*_curveDelegate, cSize, 999);

    if (ImGui::IsMouseHoveringRect(cPos, ImVec2(cPos.x + cSize.x, cPos.y + cSize.y)))
    {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
        {
            if (ImGui::GetIO().KeyCtrl)
            {
                const float nx          = (ImGui::GetIO().MousePos.x - cPos.x) / cSize.x;
                const float focus       = _curveDelegate->m_min.x + nx * (_curveDelegate->m_max.x - _curveDelegate->m_min.x);
                const float zoom        = (wheel > 0.0f) ? 0.82f : 1.22f;
                const float nr          = (_curveDelegate->m_max.x - _curveDelegate->m_min.x) * zoom;
                _curveDelegate->m_min.x = focus - nx * nr;
                _curveDelegate->m_max.x = _curveDelegate->m_min.x + nr;
            }
            else
            {
                const float pan = (_curveDelegate->m_max.x - _curveDelegate->m_min.x) * 0.12f * -wheel;
                _curveDelegate->m_min.x += pan;
                _curveDelegate->m_max.x += pan;
            }
        }
    }

    ImGui::EndTable();
}