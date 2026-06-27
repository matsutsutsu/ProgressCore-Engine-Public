// ===================================================================================
// ファイル: AnimationSequencerWindow.cpp
// 役割       : AnimSequence 編集ウィンドウの実装。
// 責務       : ImSequencer/ImCurveEdit の仲介(pImpl)、UI構築、JSON保存/ロード、
//              選択エンティティへのリアルタイムプレビュー。
// ===================================================================================
#include "AnimationSequencerWindow.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Animation/Systems/AnimationSystem.h"
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Graphics/Resource/ModelResource.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Game/Logics/Combat/CombatRosterComponent.h"

// IO / Platform
#include "Engine/Platform/Core/Dialog.h"
#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"

#include "ImSequencer.h"
#include <ImCurveEdit.h>
#include <imgui.h>
#include <json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

// -----------------------------------------------------------------------------------
// 時間変換ヘルパー
// -----------------------------------------------------------------------------------
inline int SecToFrame(float sec)
{
    return (int)(sec * 60.0f);
}
inline float FrameToSec(int frame)
{
    return (float)frame / 60.0f;
}

// ===================================================================================
// ImSequencer用のインターフェース(pImpl)実装クラス
// ===================================================================================
class AnimationSequenceImpl : public ImSequencer::SequenceInterface
{
  public:
    AnimSequence *_seq = nullptr;

    // キャッシュバッファ: [0] はマスタートラック(Duration)、[1]以降がイベント
    mutable std::vector<std::pair<int, int>> _frameCacheMap;

    AnimationSequenceImpl() = default;

    void SetSequence(AnimSequence *seq)
    {
        _seq = seq;
    }

    void SyncFrameCache() const
    {
        if (!_seq)
            return;

        // 常に「イベント数 + 1 (マスタートラック)」を確保する
        _frameCacheMap.resize(_seq->events.size() + 1);

        // [0] マスタートラック: 開始は常に0、終了は現在のアセット長(duration)
        _frameCacheMap[0].first  = 0;
        _frameCacheMap[0].second = SecToFrame(_seq->duration);

        // [1] 以降: 各イベントの開始・終了
        for (size_t i = 0; i < _seq->events.size(); ++i)
        {
            _frameCacheMap[i + 1].first  = SecToFrame(_seq->events[i].startTime);
            _frameCacheMap[i + 1].second = SecToFrame(_seq->events[i].endTime);
        }
    }

    // UI上でドラッグされたフレームを実データ(秒)に同期する
    void FlushFrameCache() const
    {
        if (!_seq || _frameCacheMap.empty())
            return;

        // ★ マスタートラックのドラッグを無効化し、アセット本来の長さ(duration)に強制ロックする
        _frameCacheMap[0].first  = 0;
        _frameCacheMap[0].second = SecToFrame(_seq->duration);

        // イベントの時間を同期
        for (size_t i = 0; i < _seq->events.size(); ++i)
        {
            if (i + 1 < _frameCacheMap.size())
            {
                _seq->events[i].startTime = FrameToSec(_frameCacheMap[i + 1].first);
                _seq->events[i].endTime   = FrameToSec(_frameCacheMap[i + 1].second);
            }
        }
    }

    virtual int GetItemCount() const override
    {
        if (!_seq)
            return 0;
        // マスタートラックが必ず存在するため、常に 1 以上になる
        return (int)_seq->events.size() + 1;
    }

    virtual int GetFrameMin() const override
    {
        return 0;
    }
    virtual int GetFrameMax() const override
    {
        if (!_seq)
            return 100;
        int maxFrames = SecToFrame(_seq->duration);
        return maxFrames > 0 ? maxFrames : 100;
    }

    virtual void BeginEdit(int /*index*/) override
    {
    }
    virtual void EndEdit() override
    {
    }

    virtual int GetItemTypeCount() const override
    {
        return (int)AnimEventType::Count;
    }

    virtual const char *GetItemTypeName(int typeIndex) const override
    {
        static const char *names[] = {
            "HitBox", "Sound", "Effect", "Blackboard",
            "SpawnHitbox", "Trail", "ComboWindow", "CancelWindow"};
        if (typeIndex < 0 || typeIndex >= (int)AnimEventType::Count)
            return "Unknown";
        return names[typeIndex];
    }

    virtual const char *GetItemLabel(int index) const override
    {
        if (!_seq)
            return "";
        if (index == 0)
            return "Master (Duration)"; // 0番目は常にマスタートラック

        int evIdx = index - 1;
        if (evIdx < 0 || evIdx >= (int)_seq->events.size())
            return "";

        static char buf[128];
        const auto &ev = _seq->events[evIdx];
        switch (ev.type)
        {
        case AnimEventType::HitBox:
            snprintf(buf, sizeof(buf), "HitBox: %s", ev.stringParam.c_str());
            break;
        case AnimEventType::Sound:
            snprintf(buf, sizeof(buf), "Sound: %s", ev.stringParam.c_str());
            break;
        case AnimEventType::Effect:
            snprintf(buf, sizeof(buf), "Effect: %s", ev.stringParam.c_str());
            break;
        case AnimEventType::Blackboard:
            snprintf(buf, sizeof(buf), "BB: %s", ev.stringParam.c_str());
            break;
        case AnimEventType::SpawnHitbox:
            snprintf(buf, sizeof(buf), "SpawnHitbox");
            break;
        case AnimEventType::Trail:
            snprintf(buf, sizeof(buf), "Trail: %s", ev.stringParam.c_str());
            break;
        case AnimEventType::ComboWindow:
            snprintf(buf, sizeof(buf), "Combo: %s", ev.stringParam.c_str());
            break;
        case AnimEventType::CancelWindow:
            snprintf(buf, sizeof(buf), "Cancel: %s", ev.stringParam.c_str());
            break;
        default:
            snprintf(buf, sizeof(buf), "Event %d", evIdx);
            break;
        }
        return buf;
    }

    virtual void Get(int index, int **start, int **end, int *type, unsigned int *color) override
    {
        if (!_seq)
            return;

        // ★ 注意: ここにあった SyncFrameCache() を削除しました！
        // 毎フレーム呼ばれる Get() 内でキャッシュを再生成するとドラッグ操作が破綻するためです。

        if (index < 0 || index >= (int)_frameCacheMap.size())
            return;

        if (start)
            *start = &_frameCacheMap[index].first;
        if (end)
            *end = &_frameCacheMap[index].second;

        // index 0 はマスタートラック、1以降が実イベント
        if (index == 0)
        {
            if (type)
                *type = -1; // 区別用
            if (color)
                *color = 0xFF555555; // マスタートラックはグレー
        }
        else
        {
            int evIdx = index - 1;
            if (type)
                *type = (int)_seq->events[evIdx].type;
            if (color)
            {
                static const unsigned int colors[] = {
                    0xFF323296, 0xFF329632, 0xFF969632, 0xFF963296,
                    0xFF1010A0, 0xFF96A032, 0xFF32A096, 0xFF505050};
                *color = colors[(int)_seq->events[evIdx].type % 8];
            }
        }
    }

    virtual void Add(int type) override
    {
        if (!_seq)
            return;
        AnimNotifyEvent ev;
        ev.type      = (AnimEventType)type;
        ev.startTime = 0.0f;
        ev.endTime   = (std::min)(0.5f, _seq->duration);

        if (type == (int)AnimEventType::Sound)
            ev.stringParam = "Cue_Default";
        if (type == (int)AnimEventType::Effect)
            ev.stringParam = "VFX_Default";
        if (type == (int)AnimEventType::HitBox)
            ev.stringParam = "Weapon_R";

        _seq->events.push_back(ev);
        SyncFrameCache();
    }

    virtual void Del(int index) override
    {
        if (!_seq || index == 0)
            return; // マスターは削除不可
        int evIdx = index - 1;
        if (evIdx >= 0 && evIdx < (int)_seq->events.size())
        {
            _seq->events.erase(_seq->events.begin() + evIdx);
            SyncFrameCache();
        }
    }

    virtual void Duplicate(int index) override
    {
        if (!_seq || index == 0)
            return; // マスターは複製不可
        int evIdx = index - 1;
        if (evIdx >= 0 && evIdx < (int)_seq->events.size())
        {
            _seq->events.push_back(_seq->events[evIdx]);
            SyncFrameCache();
        }
    }
};

// ===================================================================================
// SequencerCurveDelegate（カーブエディタ用デリゲート定義）
// ===================================================================================
struct SequencerCurveDelegate : public ImCurveEdit::Delegate
{
    AnimSequence *_seq         = nullptr;
    ImVec2        m_min        = {0.0f, 0.0f};
    ImVec2        m_max        = {1.0f, 2.0f};
    bool          visible[2]   = {true, true};
    float         animDuration = 1.0f;

    void SetSequence(AnimSequence *seq)
    {
        _seq = seq;
        if (_seq)
        {
            animDuration = _seq->duration > 0.01f ? _seq->duration : 1.0f;
            m_max.x      = animDuration;
        }
    }

    virtual size_t GetCurveCount() override
    {
        return _seq ? 2 : 0;
    }
    virtual bool IsVisible(size_t curveIndex) override
    {
        return curveIndex < 2 ? visible[curveIndex] : true;
    }
    virtual ImVec2 &GetMin() override
    {
        return m_min;
    }
    virtual ImVec2 &GetMax() override
    {
        return m_max;
    }
    virtual unsigned int GetBackgroundColor() override
    {
        return 0xFF202020;
    }

    virtual size_t GetPointCount(size_t curveIndex) override
    {
        return 0; // AnimationCurve の実装が完了したら修正
    }

    virtual uint32_t GetCurveColor(size_t curveIndex) override
    {
        return (curveIndex == 0) ? 0xFF00FFFF : 0x00FF00FF;
    }

    virtual ImVec2 *GetPoints(size_t curveIndex) override
    {
        return nullptr;
    }

    virtual int EditPoint(size_t curveIndex, int pointIndex, ImVec2 value) override
    {
        return pointIndex;
    }

    virtual void AddPoint(size_t curveIndex, ImVec2 value) override
    {
    }
};

// ===================================================================================
// AnimationSequencerWindow メンバ関数群の実装
// ===================================================================================
AnimationSequencerWindow::AnimationSequencerWindow()
    : EditorWindow("Sequencer")
{
    _sequencerImpl = std::make_unique<AnimationSequenceImpl>();
    _curveDelegate = std::make_unique<SequencerCurveDelegate>();

    // 初期状態バインド
    _sequencerImpl->SetSequence(&_editingSequence);
    _sequencerImpl->SyncFrameCache();

    SetVisible(true);
}

AnimationSequencerWindow::~AnimationSequencerWindow() = default;

static void DrawCurveEditorOverlay(ImDrawList *drawList, ImVec2 pos, ImVec2 size, ImVec2 min, ImVec2 max, float duration)
{
    float timeToX = size.x / (max.x - min.x);
    float zeroX   = pos.x - min.x * timeToX;

    drawList->AddLine(ImVec2(zeroX, pos.y), ImVec2(zeroX, pos.y + size.y), 0x44FFFFFF, 1.0f);
    float durationX = zeroX + duration * timeToX;
    drawList->AddLine(ImVec2(durationX, pos.y), ImVec2(durationX, pos.y + size.y), 0x890000FF, 1.5f);
}

// -----------------------------------------------------------------------------------
// メインの描画エントリー
// -----------------------------------------------------------------------------------
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

    // 1. エンティティ切替の検出と初期化
    ResetStateOnEntitySwitch(context, animator);

    // 2. セーブ・ロード処理
    DrawFileIO();

    // 3. アニメーション選択UI
    char nameBuf[128];
    strcpy_s(nameBuf, sizeof(nameBuf), _editingSequence.sequenceName.c_str());
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputText("Sequence Name", nameBuf, sizeof(nameBuf)))
        _editingSequence.sequenceName = nameBuf;

    ImGui::SameLine();

    // ★ ロード直後の同期 (JSONのtargetAnimNameからコンボボックスの選択位置を復元する)
    if (_prevSelectedAnimIndex == -2)
    {
        _selectedAnimIndex = 0; // 見つからなかった場合のデフォルト
        for (int i = 0; i < (int)animations.size(); ++i)
        {
            if (animations[i].name == _editingSequence.targetAnimName)
            {
                _selectedAnimIndex = i;
                break;
            }
        }
        _prevSelectedAnimIndex = _selectedAnimIndex;

        // アセットの長さに強制同期
        _editingSequence.duration = (std::max)(0.01f, animations[_selectedAnimIndex].secondsLength);
        _sequencerImpl->SyncFrameCache();
        _fitCurveView = true;
    }

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

    // ★ コンボボックスで選択が変更された時の同期
    if (_prevSelectedAnimIndex != _selectedAnimIndex)
    {
        _prevSelectedAnimIndex          = _selectedAnimIndex;
        _editingSequence.targetAnimName = animations[_selectedAnimIndex].name;

        // アセット本来の長さにシーケンスの最大長を強制ロック
        _editingSequence.duration = (std::max)(0.01f, animations[_selectedAnimIndex].secondsLength);

        _sequencerImpl->SyncFrameCache();
        _fitCurveView = true;
    }

    // 4. エディットモード切替の適用
    DrawEditModeToggle(context, animator);

    ImGui::Separator();

    // 5. メインUI（2ペインレイアウト）の描画
    if (!ImGui::BeginTable("SeqLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
        return;

    ImGui::TableSetupColumn("Timeline", ImGuiTableColumnFlags_WidthStretch, 0.7f);
    ImGui::TableSetupColumn("Inspector", ImGuiTableColumnFlags_WidthStretch, 0.3f);
    ImGui::TableNextRow();

    // ---- 左側: タイムライン ----
    ImGui::TableSetColumnIndex(0);
    DrawTimelinePane(context, animator, model);

    // ---- 右側: インスペクタ & カーブ ----
    ImGui::TableSetColumnIndex(1);
    DrawInspectorPane(context);
    DrawCurvePane();

    ImGui::EndTable();
}

// -----------------------------------------------------------------------------------
// サブ描画関数群
// -----------------------------------------------------------------------------------

void AnimationSequencerWindow::ResetStateOnEntitySwitch(EditorContext &context, AnimatorComponent *animator)
{
    if (context.selectedEntity != _lastSelectedEntity)
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

        _sequencerImpl->SetSequence(&_editingSequence);
        _sequencerImpl->SyncFrameCache();
        _lastSelectedEntity = context.selectedEntity;
    }
}

void AnimationSequencerWindow::DrawFileIO()
{
    // ★ Duration はアセットから自動取得されるため、テキスト表示(閲覧専用)に変更
    ImGui::Text("Editing: %s", _savePath.empty() ? "(New Sequence)" : _savePath.c_str());
    ImGui::SameLine();

    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Duration: %.2f s (%d frames)",
        _editingSequence.duration, SecToFrame(_editingSequence.duration));
    ImGui::SameLine();

    if (ImGui::Button("Save JSON..."))
    {
        if (auto path = FileDialog::SavePreset("AnimSequence", "Save Anim Sequence"))
        {
            _savePath                     = FileDialog::ConvertToRelativePath(*path);
            _editingSequence.sequenceName = std::filesystem::path(_savePath).stem().string();

            _sequencerImpl->SyncFrameCache();

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
        if (auto path = FileDialog::OpenPreset("AnimSequence", "Load Anim Sequence"))
        {
            std::string loadPath = FileDialog::ConvertToRelativePath(*path);
            std::string text     = FileSystem::ReadAllText(loadPath);
            if (!text.empty())
            {
                try
                {
                    nlohmann::json j = nlohmann::json::parse(text);
                    _editingSequence = j.get<AnimSequence>();
                    _savePath        = loadPath;

                    // ロード完了時にアニメーションリストとの同期を促すフラグ
                    _prevSelectedAnimIndex = -2;

                    _sequencerImpl->SetSequence(&_editingSequence);
                    _sequencerImpl->SyncFrameCache();

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
}

void AnimationSequencerWindow::DrawEditModeToggle(EditorContext &context, AnimatorComponent *animator)
{
    if (context.isAnimEditMode)
    {
        if (!_wasEditMode)
        {
            _savedSequence       = animator->currentSequence;
            _wasEditMode         = true;
            context.animEditTime = animator->currentTimer;
        }
        animator->isEditorOverride = true;
        animator->currentSequence  = &_editingSequence;
    }
    else if (_wasEditMode)
    {
        animator->currentSequence  = _savedSequence;
        animator->isEditorOverride = false;
        _savedSequence             = nullptr;
        _wasEditMode               = false;
        _isPlaying                 = false;
    }
}

void AnimationSequencerWindow::DrawTimelinePane(EditorContext &context, AnimatorComponent *animator, Model *model)
{
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Space))
        _isPlaying = !_isPlaying;

    const float dur      = _editingSequence.duration;
    const int   durFrame = SecToFrame(dur);

    if (context.isAnimEditMode)
    {
        if (_isPlaying)
        {
            context.animEditTime += ImGui::GetIO().DeltaTime * animator->playbackSpeed;
            if (dur > 0.0f && context.animEditTime > dur)
            {
                // ループ設定はプレビュー専用トグルを優先（アニメ設定と独立に試せる）
                context.animEditTime = _loopPreview ? std::fmod(context.animEditTime, dur) : dur;
                if (!_loopPreview)
                    _isPlaying = false; // 非ループは最後で停止
            }
            animator->currentTimer = context.animEditTime;
        }

        // ── 再生コントロール群 ──
        if (ImGui::Button("|<")) // 先頭へ
        {
            context.animEditTime   = 0.0f;
            animator->currentTimer = 0.0f;
            _isPlaying             = false;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("先頭へ (Home)");
        ImGui::SameLine();

        if (ImGui::Button(_isPlaying ? "|| Pause" : "> Play"))
            _isPlaying = !_isPlaying;
        ImGui::SameLine();

        if (ImGui::Button(">|")) // 最後へ
        {
            context.animEditTime   = dur;
            animator->currentTimer = dur;
            _isPlaying             = false;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("最後へ (End)");
        ImGui::SameLine();

        // ループトグル
        ImGui::Checkbox("Loop", &_loopPreview);
        ImGui::SameLine();

        // ── 現在時刻の 秒 / frame / 全体長 表示 ──
        const float curSec = context.animEditTime;
        const int   curFrm = SecToFrame(curSec);
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1.0f),
            "%.2fs / %df", curSec, curFrm);
        ImGui::SameLine();
        ImGui::TextDisabled("(total %.2fs / %df)", dur, durFrame);
    }
    else
    {
        _isPlaying = false;
        ImGui::TextDisabled("Enable 'Edit Mode' to preview & scrub.");
    }

    // ── Homeキー/Endキーでも先頭・末尾へ ──
    if (context.isAnimEditMode && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Home))
        {
            context.animEditTime   = 0.0f;
            animator->currentTimer = 0.0f;
            _isPlaying             = false;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_End))
        {
            context.animEditTime   = dur;
            animator->currentTimer = dur;
            _isPlaying             = false;
        }
    }

    // ── スナップ設定 ──
    ImGui::Checkbox("Snap", &_snapEnabled);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::DragInt("##snapStep", &_snapStep, 1, 1, 30, "%df");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("スナップ単位（フレーム）");
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // イベント追加
    auto AddEvt = [&](AnimEventType t) {
        _sequencerImpl->Add((int)t);
        auto &e     = _editingSequence.events.back();
        int   s     = context.isAnimEditMode ? SecToFrame(context.animEditTime) : 0;
        e.startTime = FrameToSec(s);
        e.endTime   = FrameToSec(s + 30);
        _sequencerImpl->SyncFrameCache();
        _selectedEntry = (int)_editingSequence.events.size();
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
    if (ImGui::Button("+ Combo"))
        AddEvt(AnimEventType::ComboWindow);
    ImGui::SameLine();
    if (ImGui::Button("+ Cancel"))
        AddEvt(AnimEventType::CancelWindow);

    int curFrame  = context.isAnimEditMode ? SecToFrame(context.animEditTime) : SecToFrame(animator->currentTimer);
    int prevFrame = curFrame;

    _expanded    = true;
    bool changed = ImSequencer::Sequencer(
        _sequencerImpl.get(), &curFrame, &_expanded, &_selectedEntry, &_firstFrame,
        ImSequencer::SEQUENCER_EDIT_STARTEND | ImSequencer::SEQUENCER_ADD |
            ImSequencer::SEQUENCER_DEL | ImSequencer::SEQUENCER_CHANGE_FRAME);

    // ── ドラッグ後のスナップ適用（選択イベントのフレーム境界を丸める）──
    if (changed)
    {
        if (_snapEnabled && _snapStep > 0 &&
            _selectedEntry > 0 && _selectedEntry < (int)_sequencerImpl->_frameCacheMap.size())
        {
            auto &f    = _sequencerImpl->_frameCacheMap[_selectedEntry];
            auto  snap = [&](int v) { return ((v + _snapStep / 2) / _snapStep) * _snapStep; };
            f.first    = snap(f.first);
            f.second   = snap(f.second);
            if (f.second <= f.first)
                f.second = f.first + _snapStep; // 区間が潰れない最小幅
        }
        _sequencerImpl->FlushFrameCache();
    }

    // 削除処理
    if (_selectedEntry > 0 && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Delete))
    {
        _sequencerImpl->Del(_selectedEntry);
        _selectedEntry = -1;
    }

    // ナッジ(微調整)処理
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            _selectedEntry = -1;

        int nudge = ImGui::GetIO().KeyShift ? 5 : 1;
        if (_selectedEntry > 0 && _selectedEntry < (int)_sequencerImpl->_frameCacheMap.size()) // マスターはナッジ不可
        {
            auto &f     = _sequencerImpl->_frameCacheMap[_selectedEntry];
            bool  moved = false;

            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
            {
                f.first -= nudge;
                f.second -= nudge;
                moved = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
            {
                f.first += nudge;
                f.second += nudge;
                moved = true;
            }
            if (moved && f.first < 0)
            {
                f.second += -f.first;
                f.first = 0;
            }

            if (moved)
                _sequencerImpl->FlushFrameCache();
        }
    }

    if (curFrame != prevFrame)
    {
        _isPlaying             = false;
        context.animEditTime   = FrameToSec(curFrame);
        animator->currentTimer = context.animEditTime;
    }

    // ECSランタイムへの反映
    if (context.isAnimEditMode && context.systemManager)
    {
        if (auto *animSys = context.systemManager->GetSystem<AnimationSystem>())
        {
            animSys->UpdateManual(context.selectedEntity, context.animEditTime);
            if (auto *tr = context.world->GetComponent<TransformComponent>(context.selectedEntity))
                model->UpdateTransform(tr->worldMatrix);
        }
    }
}

void AnimationSequencerWindow::DrawInspectorPane(EditorContext &context)
{
    ImGui::Text("Sequence Settings");
    ImGui::Separator();

    // マスター（全体の長さ）が選択されている場合、もしくは未選択の場合の表示
    if (_selectedEntry == 0 || _selectedEntry == -1)
    {
        if (_selectedEntry == 0)
        {
            ImGui::TextColored(ImVec4(0.5f, 1, 0.5f, 1), "Master Track (Duration)");
            ImGui::TextDisabled("Modify the total length of the sequence.");
        }

        // アセットロック済みなので閲覧のみ
        ImGui::Text("Duration: %.2f s", _editingSequence.duration);
        ImGui::Checkbox("Edit Mode (Preview)", &context.isAnimEditMode);
    }
    else if (_selectedEntry > 0 && _selectedEntry <= (int)_editingSequence.events.size())
    {
        auto &e  = _editingSequence.events[_selectedEntry - 1]; // イベントのインデックスは-1
        bool  ch = false;

        ImGui::TextColored(ImVec4(0.5f, 1, 0.5f, 1), "Selected Event");
        ImGui::Spacing();
        ImGui::TextDisabled("Type: %s", _sequencerImpl->GetItemTypeName((int)e.type));
        // ── 開始/終了を秒で直接入力（ドラッグより精密）──
        {
            float startSec = e.startTime;
            float endSec   = e.endTime;
            bool  timeCh   = false;

            ImGui::SetNextItemWidth(90);
            if (ImGui::DragFloat("開始 (s)##start", &startSec, 0.01f, 0.0f, _editingSequence.duration, "%.2f"))
                timeCh = true;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90);
            if (ImGui::DragFloat("終了 (s)##end", &endSec, 0.01f, 0.0f, _editingSequence.duration, "%.2f"))
                timeCh = true;

            // フレーム表示も併記
            ImGui::TextDisabled("Frame: %d - %d", SecToFrame(e.startTime), SecToFrame(e.endTime));

            if (timeCh)
            {
                // クランプと前後関係の保証
                startSec = std::clamp(startSec, 0.0f, _editingSequence.duration);
                endSec   = std::clamp(endSec, 0.0f, _editingSequence.duration);
                if (endSec < startSec)
                    endSec = startSec;

                e.startTime = startSec;
                e.endTime   = endSec;
                ch          = true;
                _sequencerImpl->SyncFrameCache(); // タイムライン側へ即反映
            }
        }
        ImGui::Spacing();
        ImGui::Spacing();

        char buf[256];
        strcpy_s(buf, sizeof(buf), e.stringParam.c_str());

        switch (e.type)
        {
        case AnimEventType::HitBox: {
            auto *roster = context.world->GetComponent<CombatRosterComponent>(context.selectedEntity);
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
        case AnimEventType::ComboWindow:
        case AnimEventType::CancelWindow: {
            const bool isCombo = (e.type == AnimEventType::ComboWindow);
            ImGui::TextColored(isCombo ? ImVec4(0.25f, 0.75f, 1.0f, 1.0f) : ImVec4(0.75f, 0.25f, 1.0f, 1.0f),
                isCombo ? "Combo Accept Window" : "Cancel Window");
            ImGui::Text("受付名 (Window Name):");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##Win", buf, sizeof(buf)))
            {
                e.stringParam = buf;
                ch            = true;
            }
            ImGui::TextDisabled("Active [%.2fs - %.2fs]", e.startTime, e.endTime);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("遷移に同名を設定すると、この区間のみ有効になります。");
            break;
        }
        default:
            break;
        }

        if (ch)
            _sequencerImpl->SyncFrameCache();
    }
}

void AnimationSequencerWindow::DrawCurvePane()
{
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
    {
        if (xR[1] > xR[0] + 0.01f)
        {
            _curveDelegate->m_min.x = xR[0];
            _curveDelegate->m_max.x = xR[1];
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Fit"))
        _fitCurveView = true;

    ImGui::Checkbox("Speed", &_curveDelegate->visible[0]);
    ImGui::SameLine();
    ImGui::Checkbox("RootMotion", &_curveDelegate->visible[1]);
    ImGui::SameLine();
    ImGui::TextDisabled("[Scroll:Pan | Ctrl+Scroll:Zoom]");

    if (_fitCurveView && _curveDelegate->_seq)
    {
        _curveDelegate->m_min.x = 0.0f;
        _curveDelegate->m_max.x = _editingSequence.duration * 1.05f;
        _fitCurveView           = false;
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
}