#include "AnimationSequencerWindow.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "ImSequencer.h"
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Graphics/Resource/ModelResource.h"
#include "Engine/Animation/Core/AnimSequence.h"
#include "Engine/Animation/Systems/AnimationSystem.h"

#include "Game/Logics/Combat/CombatRosterComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Transform/Components/PendingParentComponent.h"

#include "Engine/Platform/Core/Dialog.h"
#include "Engine/Platform/Core/Logger.h"

#include <filesystem>
#include <ImCurveEdit.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include <fstream>
#include <json.hpp>

// =============================================================================
// AnimationSequenceImpl（ImSequencer <-> AnimSequence の仲介）
// cpp 内部クラス。ヘッダから ImSequencer.h の依存を遮断する。
// =============================================================================
class AnimationSequenceImpl : public ImSequencer::SequenceInterface
{
  public:
    struct EditorEvent
    {
        int  frameStart;
        int  frameEnd;
        int  type;
        char stringParam[256];

        float floatParam1 = 0.0f;
        float floatParam2 = 0.0f;
        float floatParam3 = 0.0f;

        float offset[3] = {0.0f, 0.0f, 0.0f};
        float radius    = 1.0f;
    };

    std::vector<EditorEvent> cache;
    AnimSequence            *targetSequence = nullptr;

    int mFrameMin = 0;
    int mFrameMax = 60 * 5;

    // =========================================================================
    // 台本(ECSデータ) -> UI(ブロック) への変換
    // =========================================================================
    void SyncFromData()
    {
        cache.clear();
        if (!targetSequence)
            return;

        // ローカル変数で BB ペアを追跡（メンバ変数のシャドウを排除）
        int         pendingBBStart = -1;
        std::string pendingBBEventName;

        for (const auto &evt : targetSequence->events)
        {
            EditorEvent item{};
            item.frameStart = (int)(evt.startTime * 60.0f);
            item.frameEnd   = (int)(evt.endTime * 60.0f);
            strcpy_s(item.stringParam, sizeof(item.stringParam), evt.stringParam.c_str());
            item.floatParam1 = evt.floatParam1;
            item.floatParam2 = evt.floatParam2;
            item.floatParam3 = evt.floatParam3;
            item.offset[0]   = evt.offset.x;
            item.offset[1]   = evt.offset.y;
            item.offset[2]   = evt.offset.z;
            item.radius      = evt.radius;

            if (evt.eventName == "HitBox")
            {
                item.type = 0;
                cache.push_back(item);
            }
            else if (evt.eventName == "SpawnHitbox")
            {
                item.type      = 4;
                item.offset[0] = evt.offset.x;
                item.offset[1] = evt.offset.y;
                item.offset[2] = evt.offset.z;
                item.radius    = evt.radius;
                cache.push_back(item);
            }
            else if (evt.eventName == "Play_Sound")
            {
                item.type = 1;
                cache.push_back(item);
            }
            else if (evt.eventName == "Play_Effect")
            {
                item.type = 2;
                cache.push_back(item);
            }
            else if (evt.eventName == "Trail")
            {
                item.type = 5;
                cache.push_back(item);
            }
            else if (evt.eventName.find("BB_") == 0 &&
                     evt.eventName.find("_True") != std::string::npos)
            {
                pendingBBStart     = (int)(evt.startTime * 60.0f);
                pendingBBEventName = evt.eventName.substr(3, evt.eventName.find("_True") - 3);
            }
            else if (evt.eventName.find("BB_") == 0 &&
                     evt.eventName.find("_False") != std::string::npos)
            {
                if (pendingBBStart != -1)
                {
                    EditorEvent bbItem{};
                    bbItem.type       = 3;
                    bbItem.frameStart = pendingBBStart;
                    bbItem.frameEnd   = (int)(evt.startTime * 60.0f);
                    strcpy_s(bbItem.stringParam, sizeof(bbItem.stringParam),
                        pendingBBEventName.c_str());
                    cache.push_back(bbItem);
                    pendingBBStart = -1;
                }
            }
        }
    }

    // =========================================================================
    // UI(ブロック) -> 台本(ECSデータ) への変換と自動ソート
    // =========================================================================
    void SyncToData()
    {
        if (!targetSequence)
            return;
        targetSequence->events.clear();

        for (const auto &item : cache)
        {
            float startSec = (float)item.frameStart / 60.0f;
            float endSec   = (float)item.frameEnd / 60.0f;

            AnimNotifyEvent ev;
            ev.startTime   = startSec;
            ev.endTime     = endSec;
            ev.floatParam1 = item.floatParam1;
            ev.floatParam2 = item.floatParam2;
            ev.floatParam3 = item.floatParam3;
            ev.offset      = DirectX::SimpleMath::Vector3(
                item.offset[0], item.offset[1], item.offset[2]);
            ev.radius = item.radius;

            if (item.type == 0)
            {
                ev.eventName   = "HitBox";
                ev.stringParam = item.stringParam;
                targetSequence->events.push_back(ev);
            }
            else if (item.type == 1)
            {
                ev.eventName   = "Play_Sound";
                ev.stringParam = item.stringParam;
                targetSequence->events.push_back(ev);
            }
            else if (item.type == 2)
            {
                ev.eventName   = "Play_Effect";
                ev.stringParam = item.stringParam;
                targetSequence->events.push_back(ev);
            }
            else if (item.type == 3)
            {
                std::string baseName = item.stringParam;
                ev.eventName         = "BB_" + baseName + "_True";
                ev.stringParam       = "";
                targetSequence->events.push_back(ev);

                AnimNotifyEvent evFalse = ev;
                evFalse.startTime       = endSec;
                evFalse.eventName       = "BB_" + baseName + "_False";
                targetSequence->events.push_back(evFalse);
            }
            else if (item.type == 4)
            {
                ev.eventName = "SpawnHitbox";
                ev.offset    = DirectX::SimpleMath::Vector3(
                    item.offset[0], item.offset[1], item.offset[2]);
                ev.radius = item.radius;
                targetSequence->events.push_back(ev);
            }
            else if (item.type == 5)
            {
                ev.eventName   = "Trail";
                ev.stringParam = item.stringParam;
                targetSequence->events.push_back(ev);
            }
        }

        // startTime 昇順ソート（栞ロジックの不変条件を保証）
        std::sort(targetSequence->events.begin(), targetSequence->events.end(),
            [](const AnimNotifyEvent &a, const AnimNotifyEvent &b) {
                return a.startTime < b.startTime;
            });
    }

    void UpdateDuration(float durationSeconds)
    {
        mFrameMax = (int)(durationSeconds * 60.0f * 1.1f);
        if (mFrameMax < 60)
            mFrameMax = 60;
    }

    // ---- ImSequencer::SequenceInterface 実装 ----
    virtual int GetFrameMin() const override
    {
        return mFrameMin;
    }
    virtual int GetFrameMax() const override
    {
        return mFrameMax;
    }
    virtual int GetItemCount() const override
    {
        return (int)cache.size();
    }
    virtual int GetItemTypeCount() const override
    {
        return 6;
    }

    virtual const char *GetItemTypeName(int typeIndex) const override
    {
        switch (typeIndex)
        {
        case 0:
            return "HitBox (Attack)";
        case 1:
            return "Sound (SE)";
        case 2:
            return "Effect (Effekseer)";
        case 3:
            return "Blackboard (Flag)";
        case 4:
            return "Spawn HitBox (Spatial)";
        case 5:
            return "Trail (Weapon)";
        default:
            return "Unknown";
        }
    }

    virtual const char *GetItemLabel(int index) const override
    {
        // スレッドセーフではないが ImSequencer の呼び出し規約上は単一スレッドから呼ばれる
        static char temp[128];
        if (index < 0 || index >= (int)cache.size())
            return "";
        const auto &item = cache[index];
        const char *name = (item.stringParam[0] != '\0') ? item.stringParam : "Empty";
        switch (item.type)
        {
        case 0:
            sprintf_s(temp, "[%d] Attack: %s", index, name);
            break;
        case 1:
            sprintf_s(temp, "[%d] Sound: %s", index, name);
            break;
        case 2:
            sprintf_s(temp, "[%d] Effekseer: %s", index, name);
            break;
        case 3:
            sprintf_s(temp, "[%d] Blackboard: %s", index, name);
            break;
        case 4:
            sprintf_s(temp, "[%d] Spawn Sphere: R=%.1f", index, item.radius);
            break;
        case 5:
            sprintf_s(temp, "[%d] Trail: %s", index, name);
            break;
        default:
            sprintf_s(temp, "[%d] Event", index);
            break;
        }
        return temp;
    }

    virtual void Get(int index, int **start, int **end, int *type, unsigned int *color) override
    {
        if (index < 0 || index >= (int)cache.size())
            return;
        EditorEvent &item = cache[index];
        if (start)
            *start = &item.frameStart;
        if (end)
            *end = &item.frameEnd;
        if (type)
            *type = item.type;
        if (color)
        {
            switch (item.type)
            {
            case 0:
                *color = 0xFF5050D0;
                break; // 紫: HitBox
            case 1:
                *color = 0xFFD08050;
                break; // 橙: Sound
            case 2:
                *color = 0xFF50D050;
                break; // 緑: Effect
            case 3:
                *color = 0xFF50A0D0;
                break; // 水色: Blackboard（旧実装で欠落→修正）
            case 4:
                *color = 0xFFD050A0;
                break; // ピンク: SpawnHitbox
            case 5:
                *color = 0xFFD0D030;
                break; // 黄: Trail
            default:
                *color = 0xFFFFFFFF;
                break;
            }
        }
    }

    virtual void Add(int type) override
    {
        EditorEvent item{};
        item.type       = type;
        item.frameStart = 0;
        item.frameEnd   = (type == 0 || type == 4 || type == 5) ? 30 : 10;
        memset(item.stringParam, 0, sizeof(item.stringParam));
        if (type == 2)
            item.floatParam1 = 1.0f; // デフォルトスケール
        if (type == 4)
        {
            item.offset[0] = item.offset[1] = item.offset[2] = 0.0f;
            item.radius                                      = 1.0f;
        }
        cache.push_back(item);
    }

    virtual void Del(int index) override
    {
        if (index >= 0 && index < (int)cache.size())
            cache.erase(cache.begin() + index);
    }
    virtual void Duplicate(int index) override
    {
        if (index >= 0 && index < (int)cache.size())
            cache.push_back(cache[index]);
    }
    virtual size_t GetCustomHeight(int /*index*/) override
    {
        return 24;
    }
    virtual void DoubleClick(int /*index*/) override
    {
    }
    virtual void CustomDraw(int, ImDrawList *, const ImRect &, const ImRect &,
        const ImRect &, const ImRect &) override
    {
    }
    virtual void CustomDrawCompact(int, ImDrawList *, const ImRect &, const ImRect &) override
    {
    }
};

// =============================================================================
// SequencerCurveDelegate（AnimSequence のカーブを ImCurveEdit に橋渡し）
// =============================================================================
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
        {
            animDuration = targetSeq->duration;
            if (m_max.x <= 0.0f || m_max.x == 1.0f)
                m_max.x = targetSeq->duration;
        }
        UpdatePoints();
    }

    void FitViewToDuration()
    {
        if (!targetSeq)
            return;
        m_min.x = 0.0f;
        m_max.x = targetSeq->duration;
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
    size_t GetPointCount(size_t curveIndex) override
    {
        if (!targetSeq)
            return 0;
        return (curveIndex == 0) ? targetSeq->speedCurve.keys.size()
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
        value.x               = (std::max)(0.0f, value.x);
        AnimationCurve &curve = (ci == 0) ? targetSeq->speedCurve : targetSeq->rootMotionCurve;
        curve.keys[pi].time   = value.x;
        curve.keys[pi].value  = value.y;
        UpdatePoints();
        return pi;
    }

    void AddPoint(size_t ci, ImVec2 value) override
    {
        if (!targetSeq)
            return;
        AnimationCurve &curve = (ci == 0) ? targetSeq->speedCurve : targetSeq->rootMotionCurve;
        curve.keys.push_back({value.x, value.y});
        std::sort(curve.keys.begin(), curve.keys.end(),
            [](const CurveKey &a, const CurveKey &b) { return a.time < b.time; });
        UpdatePoints();
    }
};

// =============================================================================
// カーブエディタ グリッド＋ルーラー オーバーレイ（変更なし・省略なし）
// =============================================================================
static void DrawCurveEditorOverlay(
    ImDrawList *dl,
    ImVec2      canvasPos,
    ImVec2      canvasSize,
    ImVec2      viewMin,
    ImVec2      viewMax,
    float       animDuration)
{
    if (canvasSize.x <= 0 || canvasSize.y <= 0)
        return;
    if (viewMax.x <= viewMin.x || viewMax.y <= viewMin.y)
        return;

    const float left   = canvasPos.x;
    const float top    = canvasPos.y;
    const float right  = left + canvasSize.x;
    const float bottom = top + canvasSize.y;
    const float PAD    = 6.0f;

    dl->PushClipRect(canvasPos, ImVec2(right, bottom), true);

    const ImU32 COL_GRID_MINOR = IM_COL32(70, 70, 70, 110);
    const ImU32 COL_GRID_MAJOR = IM_COL32(100, 100, 100, 160);
    const ImU32 COL_AXIS_ZERO  = IM_COL32(180, 180, 180, 200);
    const ImU32 COL_DURATION   = IM_COL32(255, 180, 60, 200);
    const ImU32 COL_LABEL      = IM_COL32(210, 210, 210, 230);
    const ImU32 COL_LABEL_SHD  = IM_COL32(0, 0, 0, 200);

    auto toScreenX = [&](float t) {
        return left + PAD + (t - viewMin.x) / (viewMax.x - viewMin.x) * (canvasSize.x - 2 * PAD);
    };
    auto toScreenY = [&](float v) {
        return bottom - PAD - (v - viewMin.y) / (viewMax.y - viewMin.y) * (canvasSize.y - 2 * PAD);
    };

    // X グリッド（時間軸）
    float xRange = viewMax.x - viewMin.x;
    float xStep  = 0.1f;
    if (xRange > 5.0f)
        xStep = 0.5f;
    if (xRange > 20.0f)
        xStep = 1.0f;

    for (float t = std::ceil(viewMin.x / xStep) * xStep; t <= viewMax.x; t += xStep)
    {
        float sx = toScreenX(t);
        if (sx < left || sx > right)
            continue;
        bool major = (std::fmod(std::abs(t), xStep * 5.0f) < xStep * 0.5f);
        dl->AddLine(ImVec2(sx, top), ImVec2(sx, bottom),
            major ? COL_GRID_MAJOR : COL_GRID_MINOR, major ? 1.0f : 0.5f);
        if (major)
        {
            char label[16];
            snprintf(label, sizeof(label), "%.2fs", t);
            dl->AddText(ImVec2(sx + 2.0f + 1.0f, top + 2.0f + 1.0f), COL_LABEL_SHD, label);
            dl->AddText(ImVec2(sx + 2.0f, top + 2.0f), COL_LABEL, label);
        }
    }

    // Y グリッド（値軸）
    float yRange = viewMax.y - viewMin.y;
    float yStep  = 0.5f;
    if (yRange > 5.0f)
        yStep = 1.0f;

    for (float v = std::ceil(viewMin.y / yStep) * yStep; v <= viewMax.y; v += yStep)
    {
        float sy = toScreenY(v);
        if (sy < top || sy > bottom)
            continue;
        bool major = (std::fmod(std::abs(v), yStep * 2.0f) < yStep * 0.5f);
        dl->AddLine(ImVec2(left, sy), ImVec2(right, sy),
            major ? COL_GRID_MAJOR : COL_GRID_MINOR, major ? 1.0f : 0.5f);
        if (major)
        {
            char label[16];
            snprintf(label, sizeof(label), "%.1f", v);
            dl->AddText(ImVec2(left + PAD + 1.0f + 1.0f, sy - 10.0f + 1.0f), COL_LABEL_SHD, label);
            dl->AddText(ImVec2(left + PAD + 1.0f, sy - 10.0f), COL_LABEL, label);
        }
    }

    // Y=0 ライン（軸）
    if (viewMin.y <= 0.0f && 0.0f <= viewMax.y)
    {
        float sy = toScreenY(0.0f);
        dl->AddLine(ImVec2(left, sy), ImVec2(right, sy), COL_AXIS_ZERO, 1.5f);
    }

    // Duration 縦線
    if (animDuration > viewMin.x && animDuration < viewMax.x)
    {
        float sx = toScreenX(animDuration);
        dl->AddLine(ImVec2(sx, top), ImVec2(sx, bottom), COL_DURATION, 2.0f);
        dl->AddText(ImVec2(sx + 3.0f, bottom - 18.0f), COL_DURATION, "END");
    }

    dl->PopClipRect();
}

// =============================================================================
// AnimationSequencerWindow 実装
// =============================================================================
AnimationSequencerWindow::AnimationSequencerWindow()
    : EditorWindow("Sequencer")
{
    _sequencerImpl = std::make_unique<AnimationSequenceImpl>();
    _curveDelegate = std::make_unique<SequencerCurveDelegate>();

    // 初回から空のタイムラインを表示するために紐付け
    _sequencerImpl->targetSequence = &_editingSequence;
    _sequencerImpl->UpdateDuration(_editingSequence.duration);
    _sequencerImpl->SyncFromData();

    SetVisible(true);
}

AnimationSequencerWindow::~AnimationSequencerWindow() = default;

void AnimationSequencerWindow::DrawContents(EditorContext &context)
{
    if (context.selectedEntity == CCL::ECS::InvalidEntityID ||
        context.selectedEntity == 0)
    {
        ImGui::TextDisabled("Select an entity to edit animation.");
        return;
    }

    CCL::ECS::EntityID entity = context.selectedEntity;

    auto *animator  = context.world->GetComponent<AnimatorComponent>(entity);
    auto *modelComp = context.world->GetComponent<ModelComponent>(entity);
    if (!animator || !modelComp || !modelComp->GetModel())
    {
        ImGui::TextColored(ImVec4(1, 0, 0, 1),
            "Entity needs Animator and Model components.");
        return;
    }

    Model         *model    = modelComp->GetModel();
    ModelResource *resource = model->GetResourceMutable();
    if (!resource)
        return;

    auto &animations = resource->GetAnimationsMutable();
    if (animations.empty())
        return;

    // =========================================================
    // エンティティ切り替え検出：選択が変わったら状態をリセット
    // =========================================================
    if (entity != _lastSelectedEntity)
    {
        // 前のエンティティで編集モードが残っていたら安全に解除
        if (context.isAnimEditMode)
        {
            if (animator)
            {
                animator->isEditorOverride = false;
                // 退避していたシーケンスを復元（前エンティティとは別物だが安全側に倒す）
                if (_savedSequence)
                    animator->currentSequence = _savedSequence;
            }
            context.isAnimEditMode = false;
        }

        // 編集バッファを空のシーケンスにリセット
        _editingSequence       = AnimSequence{};
        _savePath              = "";
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
        _sequencerImpl->SyncFromData();

        _lastSelectedEntity = entity;
    }

    // =========================================================
    // 1. トップツールバー（セーブ・ロード）
    // =========================================================
    if (ImGui::Button("Save JSON..."))
    {
        char filename[MAX_PATH] = {};
        if (Dialog::SaveFileName(filename, MAX_PATH,
                "JSON Files\0*.json\0", "Save Anim Sequence", "json",
                "Data/Animations/Sequence", GetActiveWindow()) == DialogResult::OK)
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            fs::path        relPath   = fs::relative(filename, fs::current_path(), ec);
            std::string     finalPath = (!ec && !relPath.empty())
                                            ? relPath.generic_string()
                                            : filename;
            _savePath                 = finalPath;

            // ファイル名からシーケンス名を自動抽出
            _editingSequence.sequenceName = fs::path(finalPath).stem().string();
            _sequencerImpl->SyncToData();

            std::ofstream os(finalPath);
            if (os.is_open())
            {
                nlohmann::json j;
                j["AnimSequence"] = _editingSequence;
                os << j.dump(4);
                CCL_LOG_INFO(LogCategory::Editor, "Sequence Saved: %s", finalPath.c_str());
            }
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Load JSON..."))
    {
        char filename[MAX_PATH] = {};
        if (Dialog::OpenFileName(filename, MAX_PATH,
                "JSON Files\0*.json\0", "Load Anim Sequence",
                "Data/Animations/Sequence", GetActiveWindow()) == DialogResult::OK)
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            fs::path        relPath   = fs::relative(filename, fs::current_path(), ec);
            std::string     finalPath = (!ec && !relPath.empty())
                                            ? relPath.generic_string()
                                            : filename;
            _savePath                 = finalPath;

            std::ifstream is(finalPath);
            if (is.is_open())
            {
                try
                {
                    nlohmann::json j;
                    is >> j;
                    if (j.contains("AnimSequence"))
                        j.at("AnimSequence").get_to(_editingSequence);
                    else
                        j.get_to(_editingSequence);

                    // ロード後にターゲットアニメを同期
                    _selectedAnimIndex = 0;
                    for (int i = 0; i < (int)animations.size(); ++i)
                    {
                        if (animations[i].m_Name == _editingSequence.targetAnimName)
                        {
                            _selectedAnimIndex = i;
                            break;
                        }
                    }
                    _prevSelectedAnimIndex = -2; // 強制再バインドを促す

                    _sequencerImpl->targetSequence = &_editingSequence;
                    _sequencerImpl->UpdateDuration(_editingSequence.duration);
                    _sequencerImpl->SyncFromData();
                    _selectedEntry = -1;
                    _fitCurveView  = true;

                    CCL_LOG_SUCCESS(LogCategory::Editor,
                        "Sequence Loaded: %s", finalPath.c_str());
                }
                catch (const std::exception &e)
                {
                    CCL_LOG_ERROR(LogCategory::Editor,
                        "Failed to load sequence JSON: %s", e.what());
                }
            }
        }
    }

    ImGui::SameLine();
    ImGui::TextDisabled("File: %s", _savePath.empty() ? "None" : _savePath.c_str());

    // シーケンス名の編集
    char seqNameBuf[128] = {};
    strcpy_s(seqNameBuf, sizeof(seqNameBuf), _editingSequence.sequenceName.c_str());
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::InputText("Sequence Name", seqNameBuf, sizeof(seqNameBuf)))
        _editingSequence.sequenceName = seqNameBuf;

    // ターゲットアニメーションのコンボ
    ImGui::SameLine();
    if (_selectedAnimIndex < 0 || _selectedAnimIndex >= (int)animations.size())
        _selectedAnimIndex = 0;

    ImGui::SetNextItemWidth(250.0f);
    if (ImGui::BeginCombo("Target Animation",
            animations[_selectedAnimIndex].m_Name.c_str()))
    {
        for (int i = 0; i < (int)animations.size(); ++i)
        {
            bool isSelected = (_selectedAnimIndex == i);
            if (ImGui::Selectable(animations[i].m_Name.c_str(), isSelected))
                _selectedAnimIndex = i;
            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    // アニメ選択が変わったらバッファに反映
    if (_prevSelectedAnimIndex != _selectedAnimIndex)
    {
        _prevSelectedAnimIndex          = _selectedAnimIndex;
        _editingSequence.targetAnimName = animations[_selectedAnimIndex].m_Name;
        _editingSequence.duration       = animations[_selectedAnimIndex].m_SecondsLength;
        _sequencerImpl->targetSequence  = &_editingSequence;
        _sequencerImpl->UpdateDuration(_editingSequence.duration);
        _sequencerImpl->SyncFromData();
        _fitCurveView = true;
    }

    // =========================================================
    // Edit Mode の制御
    // =========================================================
    if (context.isAnimEditMode)
    {
        // ── 編集モード突入時の1回だけ実行 ──────────────────────────
        if (!_wasEditMode)
        {
            // ★修正: 元のシーケンスを退避してから編集バッファで上書き
            _savedSequence       = animator->currentSequence;
            _wasEditMode         = true;
            context.animEditTime = animator->currentTimer;
        }

        // 編集バッファを Animator に指す（毎フレーム設定して上書きを防ぐ）
        animator->isEditorOverride = true;
        animator->currentSequence  = &_editingSequence;
    }
    else
    {
        if (_wasEditMode)
        {
            // ★修正: 編集モード解除時に currentSequence を退避先から復元
            animator->currentSequence  = _savedSequence;
            animator->isEditorOverride = false;
            _savedSequence             = nullptr;
            _wasEditMode               = false;
            _isPlaying                 = false;
        }
    }

    ImGui::Separator();

    // =========================================================
    // 2. メインレイアウト（左右 2ペイン）
    // =========================================================
    if (!ImGui::BeginTable("SequencerLayout", 2,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
        return;

    ImGui::TableSetupColumn("Timeline", ImGuiTableColumnFlags_WidthStretch, 0.75f);
    ImGui::TableSetupColumn("Inspector", ImGuiTableColumnFlags_WidthStretch, 0.25f);
    ImGui::TableNextRow();

    // -----------------------------------------------------
    // 左カラム: タイムライン
    // -----------------------------------------------------
    ImGui::TableSetColumnIndex(0);

    // Space で再生/停止
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        ImGui::IsKeyPressed(ImGuiKey_Space))
    {
        _isPlaying = !_isPlaying;
    }

    if (context.isAnimEditMode)
    {
        if (_isPlaying)
        {
            float dt = ImGui::GetIO().DeltaTime;
            context.animEditTime += dt * animator->playbackSpeed;
            if (context.animEditTime > _editingSequence.duration)
            {
                context.animEditTime = animator->isLoop
                                           ? fmod(context.animEditTime, _editingSequence.duration)
                                           : _editingSequence.duration;
            }
            animator->currentTimer = context.animEditTime;
        }

        if (ImGui::Button(_isPlaying ? "|| Pause" : "> Play"))
            _isPlaying = !_isPlaying;
        ImGui::SameLine();
    }
    else
    {
        _isPlaying = false;
    }

    // イベント追加ヘルパー
    auto AddAndSelectEvent = [&](int type) {
        _sequencerImpl->Add(type);
        auto &newItem      = _sequencerImpl->cache.back();
        newItem.frameStart = context.isAnimEditMode
                                 ? (int)(context.animEditTime * 60.0f)
                                 : 0;
        newItem.frameEnd   = newItem.frameStart + ((type == 0) ? 30 : 10);
        _sequencerImpl->SyncToData();
        _selectedEntry = (int)_sequencerImpl->cache.size() - 1;
    };

    if (ImGui::Button("+ Attack (HitBox)"))
        AddAndSelectEvent(0);
    ImGui::SameLine();
    if (ImGui::Button("+ Spawn HitBox"))
        AddAndSelectEvent(4);
    ImGui::SameLine();
    if (ImGui::Button("+ Trail"))
        AddAndSelectEvent(5);
    ImGui::SameLine();
    if (ImGui::Button("+ Sound (SE)"))
        AddAndSelectEvent(1);
    ImGui::SameLine();
    if (ImGui::Button("+ Effect (VFX)"))
        AddAndSelectEvent(2);
    if (ImGui::Button("+ BB Flag"))
        AddAndSelectEvent(3);

    int currentFrame = context.isAnimEditMode
                           ? (int)(context.animEditTime * 60.0f)
                           : (int)(animator->currentTimer * 60.0f);
    int prevFrame    = currentFrame;

    if (_sequencerImpl->targetSequence)
    {
        _expanded    = true;
        bool changed = ImSequencer::Sequencer(
            _sequencerImpl.get(), &currentFrame, &_expanded,
            &_selectedEntry, &_firstFrame,
            ImSequencer::SEQUENCER_EDIT_STARTEND |
                ImSequencer::SEQUENCER_ADD |
                ImSequencer::SEQUENCER_DEL |
                ImSequencer::SEQUENCER_CHANGE_FRAME);

        if (changed)
            _sequencerImpl->SyncToData();

        // Delete キーで選択ブロック削除
        if (_selectedEntry >= 0 &&
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            ImGui::IsKeyPressed(ImGuiKey_Delete))
        {
            _sequencerImpl->Del(_selectedEntry);
            _sequencerImpl->SyncToData();
            _selectedEntry = -1;
        }

        // タイムライン操作
        ImVec2   seqMin = ImGui::GetItemRectMin();
        ImVec2   seqMax = ImGui::GetItemRectMax();
        ImGuiIO &io     = ImGui::GetIO();

        // Shift + ホイールで横スクロール
        if (ImGui::IsMouseHoveringRect(seqMin, seqMax))
        {
            if (io.KeyShift && io.MouseWheel != 0.0f)
            {
                _firstFrame -= (int)(io.MouseWheel * 15.0f);
                if (_firstFrame < 0)
                    _firstFrame = 0;
            }
        }

        // キーボードナッジ
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                _selectedEntry = -1;

            int nudge = io.KeyShift ? 5 : 1;

            if (_selectedEntry >= 0 &&
                _selectedEntry < (int)_sequencerImpl->cache.size())
            {
                auto &item  = _sequencerImpl->cache[_selectedEntry];
                bool  moved = false;
                if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
                {
                    item.frameStart -= nudge;
                    item.frameEnd -= nudge;
                    moved = true;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
                {
                    item.frameStart += nudge;
                    item.frameEnd += nudge;
                    moved = true;
                }
                if (moved)
                {
                    if (item.frameStart < 0)
                    {
                        item.frameEnd += (0 - item.frameStart);
                        item.frameStart = 0;
                    }
                    _sequencerImpl->SyncToData();
                }
            }
            else
            {
                if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
                {
                    currentFrame -= nudge;
                    if (currentFrame < 0)
                        currentFrame = 0;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
                {
                    currentFrame += nudge;
                    if (currentFrame > _sequencerImpl->mFrameMax)
                        currentFrame = _sequencerImpl->mFrameMax;
                }
            }
        }

        if (currentFrame != prevFrame)
        {
            _isPlaying             = false;
            context.animEditTime   = (float)currentFrame / 60.0f;
            animator->currentTimer = context.animEditTime;
        }

        if (context.isAnimEditMode && context.systemManager)
        {
            if (auto *animSystem =
                    context.systemManager->GetSystem<AnimationSystem>())
            {
                animSystem->UpdateManual(entity, context.animEditTime);
                if (auto *transform =
                        context.world->GetComponent<TransformComponent>(entity))
                {
                    model->UpdateTransform(transform->worldMatrix);
                }
            }
        }
    }

    // -----------------------------------------------------
    // 右カラム: カーブエディタ + インスペクタ
    // -----------------------------------------------------
    ImGui::TableSetColumnIndex(1);

    ImGui::Text("Duration: %.2fs", _editingSequence.duration);
    ImGui::Checkbox("Edit Mode (Preview)", &context.isAnimEditMode);
    ImGui::Separator();

    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Selected Event");
    ImGui::Spacing();

    if (_selectedEntry >= 0 && _selectedEntry < (int)_sequencerImpl->cache.size())
    {
        auto &selectedItem = _sequencerImpl->cache[_selectedEntry];
        bool  propChanged  = false;

        ImGui::TextDisabled("Type: %s",
            _sequencerImpl->GetItemTypeName(selectedItem.type));
        ImGui::Text("Time: %.2fs - %.2fs",
            selectedItem.frameStart / 60.0f, selectedItem.frameEnd / 60.0f);
        ImGui::Spacing();

        if (selectedItem.type == 0 || selectedItem.type == 4 ||
            selectedItem.type == 5)
        {
            if (selectedItem.type == 0 || selectedItem.type == 5)
            {
                auto *roster = context.world->GetComponent<CombatRosterComponent>(entity);
                if (roster && roster->count > 0)
                {
                    ImGui::Text(selectedItem.type == 0
                                    ? "Target Weapon Tag:"
                                    : "Trail Weapon Tag:");
                    const char *preview = selectedItem.stringParam[0] != '\0'
                                              ? selectedItem.stringParam
                                              : "Select Tag...";
                    ImGui::SetNextItemWidth(-1);
                    ImGui::PushID(&selectedItem);
                    if (ImGui::BeginCombo("##WeaponTag", preview))
                    {
                        for (int i = 0; i < roster->count; ++i)
                        {
                            bool isSel = (strcmp(selectedItem.stringParam,
                                              roster->entries[i].tag) == 0);
                            if (ImGui::Selectable(roster->entries[i].tag, isSel))
                            {
                                strcpy_s(selectedItem.stringParam,
                                    sizeof(selectedItem.stringParam),
                                    roster->entries[i].tag);
                                propChanged = true;
                            }
                            if (isSel)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopID();

                    // Trail の Live Edit
                    if (selectedItem.type == 5 &&
                        selectedItem.stringParam[0] != '\0')
                    {
                        CCL::ECS::EntityID targetWeaponID =
                            CCL::ECS::InvalidEntityID;
                        for (int i = 0; i < roster->count; ++i)
                        {
                            if (strcmp(selectedItem.stringParam,
                                    roster->entries[i].tag) == 0)
                            {
                                targetWeaponID = roster->entries[i].id;
                                break;
                            }
                        }
                        if (targetWeaponID != CCL::ECS::InvalidEntityID)
                        {
                            auto *trail =
                                context.world->GetComponent<TrailComponent>(
                                    targetWeaponID);
                            if (trail)
                            {
                                ImGui::Spacing();
                                ImGui::Separator();
                                ImGui::TextColored(
                                    ImVec4(0.3f, 1.0f, 1.0f, 1.0f),
                                    "Live Edit: Trail Component");
                                ImGui::ColorEdit4("Color", &trail->color.x);
                                ImGui::DragFloat3("Rotation Offset",
                                    &trail->localRotationOffset.x, 0.01f);
                                ImGui::DragFloat3("Root Offset",
                                    &trail->localRootOffset.x, 0.01f);
                                ImGui::DragFloat3("Tip Offset",
                                    &trail->localTipOffset.x, 0.01f);

                                char texBuf[256];
                                strcpy_s(texBuf, trail->texturePath.c_str());
                                ImGui::SetNextItemWidth(-1);
                                if (ImGui::InputText("##TrailTex", texBuf,
                                        sizeof(texBuf)))
                                {
                                    trail->texturePath = texBuf;
                                }
                                if (ImGui::Button("Browse Texture...",
                                        ImVec2(-1, 0)))
                                {
                                    char fname[256] = {};
                                    if (Dialog::OpenFileName(fname, 256,
                                            "Textures\0*.png;*.dds;*.jpg\0"
                                            "All Files\0*.*\0",
                                            "Select Texture", "Data/Textures",
                                            GetActiveWindow()) == DialogResult::OK)
                                    {
                                        namespace fs = std::filesystem;
                                        std::error_code ec;
                                        trail->texturePath =
                                            fs::relative(fname,
                                                fs::current_path(), ec)
                                                .generic_string();
                                    }
                                }
                            }
                            else
                            {
                                ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1),
                                    "[Warning] No TrailComponent.");
                            }
                        }
                    }
                }
                else
                {
                    ImGui::TextColored(ImVec4(1, 1, 0, 1), "[Warning] No Roster");
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::InputText("##DamageTag",
                            selectedItem.stringParam,
                            sizeof(selectedItem.stringParam)))
                        propChanged = true;
                }
            }

            if (selectedItem.type == 4)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                    "Hitbox Transform");
                if (ImGui::DragFloat3("Offset", selectedItem.offset, 0.05f))
                    propChanged = true;
                if (ImGui::DragFloat("Radius", &selectedItem.radius,
                        0.05f, 0.0f, 100.0f))
                    propChanged = true;
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                "Hit Stop Settings");

            if (selectedItem.type == 0)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
                    "Attack Settings");
                if (ImGui::DragFloat("Damage", &selectedItem.floatParam3,
                        0.5f, 0.0f, 1000.0f, "%.1f"))
                    propChanged = true;
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                    "Hit Stop Settings");
                if (ImGui::SliderFloat("HitStop Duration",
                        &selectedItem.floatParam1, 0.0f, 1.0f, "%.2f s"))
                    propChanged = true;
                if (ImGui::SliderFloat("HitStop TimeScale",
                        &selectedItem.floatParam2, 0.0f, 1.0f, "%.2f x"))
                    propChanged = true;
            }
            else if (selectedItem.type == 4)
            {
                if (ImGui::DragFloat("Damage", &selectedItem.floatParam1,
                        0.5f, 0.0f, 1000.0f, "%.1f"))
                    propChanged = true;
                if (ImGui::SliderFloat("HitStop Duration",
                        &selectedItem.floatParam2, 0.0f, 1.0f, "%.2f s"))
                    propChanged = true;
            }
        }
        else if (selectedItem.type == 1)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                "FMOD Event Path:");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##FMODPath", selectedItem.stringParam,
                    sizeof(selectedItem.stringParam)))
                propChanged = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Example: event:/SFX/SwordSwing");
        }
        else if (selectedItem.type == 2)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                "Effekseer Asset (.efk / .efkefc)");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##EffectPath", selectedItem.stringParam,
                    sizeof(selectedItem.stringParam)))
                propChanged = true;
            if (ImGui::Button("Browse Effekseer...", ImVec2(-1, 0)))
            {
                char fname[256] = {};
                if (Dialog::OpenFileName(fname, 256,
                        "Effekseer Files\0*.efk;*.efkefc\0All Files\0*.*\0",
                        "Select File", "Data", GetActiveWindow()) == DialogResult::OK)
                {
                    namespace fs = std::filesystem;
                    std::error_code ec;
                    strcpy_s(selectedItem.stringParam,
                        sizeof(selectedItem.stringParam),
                        fs::relative(fname, fs::current_path(), ec)
                            .generic_string()
                            .c_str());
                    propChanged = true;
                }
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                "Transform Settings");
            if (ImGui::DragFloat3("Offset", selectedItem.offset, 0.05f))
                propChanged = true;
            if (ImGui::DragFloat("Scale", &selectedItem.floatParam1,
                    0.05f, 0.0f, 100.0f, "%.2f x"))
                propChanged = true;
            bool isFollowing = (selectedItem.floatParam2 > 0.5f);
            if (ImGui::Checkbox("Follow Parent", &isFollowing))
            {
                selectedItem.floatParam2 = isFollowing ? 1.0f : 0.0f;
                propChanged              = true;
            }
        }
        else if (selectedItem.type == 3)
        {
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "Blackboard Flag Settings");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 20.0f);
            ImGui::Text("Quick Presets:");

            auto PresetButton = [&](const char *label, const char *flag,
                                    ImVec4 color) {
                ImGui::PushStyleColor(ImGuiCol_Button, color);
                if (ImGui::Button(label))
                {
                    strcpy_s(selectedItem.stringParam,
                        sizeof(selectedItem.stringParam), flag);
                    propChanged = true;
                }
                ImGui::PopStyleColor();
            };

            PresetButton("Accept Input", "AcceptInput",
                ImVec4(0.2f, 0.4f, 0.8f, 1.0f));
            ImGui::SameLine();
            PresetButton("Attack Next", "AttackNext",
                ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            ImGui::SameLine();
            PresetButton("Invincible", "IsInvincible",
                ImVec4(0.6f, 0.6f, 0.1f, 1.0f));
            ImGui::SameLine();
            PresetButton("Dodge Cancel", "CanDodgeCancel",
                ImVec4(0.1f, 0.7f, 0.3f, 1.0f));

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::SetNextItemWidth(
                ImGui::GetContentRegionAvail().x - 100.0f);
            if (ImGui::InputText("Flag Name", selectedItem.stringParam,
                    sizeof(selectedItem.stringParam)))
                propChanged = true;

            ImGui::BeginChild("BB_Preview", ImVec2(0, 60), true);
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                "Generated Events:");
            ImGui::BulletText("Frame %d: BB_%s_True",
                selectedItem.frameStart, selectedItem.stringParam);
            ImGui::BulletText("Frame %d: BB_%s_False",
                selectedItem.frameEnd, selectedItem.stringParam);
            ImGui::EndChild();
        }

        if (propChanged)
            _sequencerImpl->SyncToData();
    }
    else
    {
        ImGui::TextDisabled(
            "No event block selected.\nClick a block on the timeline.");
    }

    // =====================================================
    // カーブエディタ
    // =====================================================
    if (_sequencerImpl->targetSequence)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
            "◆ Animation Curves");

        ImGui::SetNextItemWidth(130);
        float yRange[2] = {_curveDelegate->m_min.y, _curveDelegate->m_max.y};
        if (ImGui::DragFloat2("Y-Range", yRange, 0.05f))
        {
            _curveDelegate->m_min.y = yRange[0];
            _curveDelegate->m_max.y = yRange[1];
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("カーブの値(Y軸)の表示範囲");
        ImGui::SameLine();

        ImGui::SetNextItemWidth(130);
        float xRange[2] = {_curveDelegate->m_min.x, _curveDelegate->m_max.x};
        if (ImGui::DragFloat2("X-Range", xRange, 0.01f, 0.0f, 100.0f))
        {
            if (xRange[1] > xRange[0] + 0.01f)
            {
                _curveDelegate->m_min.x = xRange[0];
                _curveDelegate->m_max.x = xRange[1];
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("時間(X軸)の表示範囲");
        ImGui::SameLine();

        if (ImGui::Button("Fit"))
            _fitCurveView = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("アニメーションの長さに合わせてX軸をリセット");

        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
        ImGui::Checkbox("Speed", &_curveDelegate->visible[0]);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.0f, 0.65f, 1.0f, 1.0f));
        ImGui::Checkbox("RootMotion", &_curveDelegate->visible[1]);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("[Scroll:Pan | Ctrl+Scroll:Zoom]");

        if (_fitCurveView && _curveDelegate->targetSeq)
        {
            _curveDelegate->FitViewToDuration();
            _fitCurveView = false;
        }

        _curveDelegate->SetSequence(&_editingSequence);

        ImVec2      curveCanvasPos  = ImGui::GetCursorScreenPos();
        ImVec2      availRegion     = ImGui::GetContentRegionAvail();
        const float CURVE_H         = 260.0f;
        ImVec2      curveCanvasSize = ImVec2(availRegion.x, CURVE_H);

        DrawCurveEditorOverlay(
            ImGui::GetWindowDrawList(),
            curveCanvasPos, curveCanvasSize,
            _curveDelegate->m_min, _curveDelegate->m_max,
            _curveDelegate->animDuration);

        ImCurveEdit::Edit(*_curveDelegate, curveCanvasSize, 999);

        if (ImGui::IsMouseHoveringRect(curveCanvasPos,
                ImVec2(curveCanvasPos.x + curveCanvasSize.x,
                    curveCanvasPos.y + curveCanvasSize.y)))
        {
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f)
            {
                if (ImGui::GetIO().KeyCtrl)
                {
                    const float mouseNX =
                        (ImGui::GetIO().MousePos.x - curveCanvasPos.x) / curveCanvasSize.x;
                    const float focusT =
                        _curveDelegate->m_min.x + mouseNX * (_curveDelegate->m_max.x - _curveDelegate->m_min.x);
                    const float zoomFactor = (wheel > 0.0f) ? 0.82f : 1.22f;
                    const float newRange =
                        (_curveDelegate->m_max.x - _curveDelegate->m_min.x) * zoomFactor;
                    _curveDelegate->m_min.x = focusT - mouseNX * newRange;
                    _curveDelegate->m_max.x =
                        _curveDelegate->m_min.x + newRange;
                }
                else
                {
                    const float panStep =
                        (_curveDelegate->m_max.x - _curveDelegate->m_min.x) * 0.12f * -wheel;
                    _curveDelegate->m_min.x += panStep;
                    _curveDelegate->m_max.x += panStep;
                }
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
    }

    ImGui::EndTable();
}