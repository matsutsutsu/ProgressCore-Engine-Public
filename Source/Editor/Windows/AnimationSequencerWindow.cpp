#include "AnimationSequencerWindow.h"

// 必要なヘッダー
#include "Engine/GamePlay/Animation/AnimatorComponent.h"
#include "Engine/GamePlay/Graphics/Core/ModelComponent.h"
#include "ImSequencer.h"
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Graphics/Resource/ModelResource.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

// =============================================================================
// データ管理クラス (ImSequencer <-> ModelResource の仲介)
// =============================================================================
class AnimationSequenceImpl : public ImSequencer::SequenceInterface {
  public:
    // ImSequencer用のキャッシュデータ
    struct EditorEvent {
        int frameStart;
        int frameEnd;
        int type;
        // 元データを保持
        ModelResource::AnimationEvent originalData;
    };

    std::vector<EditorEvent>                    cache;
    std::vector<ModelResource::AnimationEvent> *targetEvents = nullptr;

    int mFrameMin = 0;
    int mFrameMax = 60 * 5; // デフォルト5秒

    // -------------------------------------------------------------------------
    // 同期処理
    // -------------------------------------------------------------------------

    void SyncFromData()
    {
        cache.clear();
        if (!targetEvents) return;

        for (const auto &evt : *targetEvents) {
            EditorEvent item;
            item.originalData = evt;
            item.type         = evt.type;

            item.frameStart = (int)(evt.time * 60.0f);
            item.frameEnd   = (int)((evt.time + evt.duration) * 60.0f);

            // 終了フレームが開始より前にならないように補正
            if (item.frameEnd < item.frameStart + 1) item.frameEnd = item.frameStart + 1;

            cache.push_back(item);
        }
    }

    void SyncToData()
    {
        if (!targetEvents) return;
        targetEvents->clear();

        for (const auto &item : cache) {
            ModelResource::AnimationEvent evt = item.originalData;
            evt.type                          = item.type;

            evt.time     = (float)item.frameStart / 60.0f;
            evt.duration = (float)(item.frameEnd - item.frameStart) / 60.0f;

            if (evt.duration < 0.0f) evt.duration = 0.0f;

            targetEvents->push_back(evt);
        }
    }

    void UpdateDuration(float durationSeconds)
    {
        // 少し余裕を持たせる（+10%）
        mFrameMax = (int)(durationSeconds * 60.0f * 1.1f);
        if (mFrameMax < 60) mFrameMax = 60;
    }

    // -------------------------------------------------------------------------
    // ImSequencer Interface
    // -------------------------------------------------------------------------
    virtual int GetFrameMin() const override { return mFrameMin; }
    virtual int GetFrameMax() const override { return mFrameMax; }
    virtual int GetItemCount() const override { return (int)cache.size(); }

    virtual int GetItemTypeCount() const override { return 3; } // タイプ数を増やす

    virtual const char *GetItemTypeName(int typeIndex) const override
    {
        switch (typeIndex) {
        case 0: return "HitBox (Attack)";
        case 1: return "Sound (SE)";
        case 2: return "Effect (VFX)";
        default: return "Unknown";
        }
    }

    virtual const char *GetItemLabel(int index) const override
    {
        static char temp[64];
        if (index < 0 || index >= (int)cache.size()) return "";

        const auto &item = cache[index];
        switch (item.type) {
        case 0: sprintf_s(temp, "[%d] Attack", index); break;
        case 1: sprintf_s(temp, "[%d] Sound", index); break;
        case 2: sprintf_s(temp, "[%d] Effect", index); break;
        default: sprintf_s(temp, "[%d] Event", index); break;
        }
        return temp;
    }

    virtual void Get(int index, int **start, int **end, int *type, unsigned int *color) override
    {
        if (index < 0 || index >= (int)cache.size()) return;

        EditorEvent &item = cache[index];
        if (start) *start = &item.frameStart;
        if (end) *end = &item.frameEnd;
        if (type) *type = item.type;

        if (color) {
            // タイプごとの色分け (ABGR形式であることに注意)
            switch (item.type) {
            case 0: *color = 0xFF5050D0; break; // 赤 (HitBox)
            case 1: *color = 0xFFD08050; break; // 青 (Sound)
            case 2: *color = 0xFF50D050; break; // 緑 (Effect)
            default: *color = 0xFFFFFFFF; break;
            }
        }
    }

    virtual void Add(int type) override
    {
        EditorEvent item;
        item.type       = type;
        item.frameStart = 0;
        // HitBox(0)は持続あり、それ以外は1フレーム(瞬間)
        item.frameEnd = (type == 0) ? 30 : 1;

        item.originalData.type        = type;
        item.originalData.stringParam = "";
        item.originalData.floatParam  = 1.0f;

        cache.push_back(item);
    };

    virtual void Del(int index) override
    {
        if (index >= 0 && index < (int)cache.size()) {
            cache.erase(cache.begin() + index);
        }
    }

    virtual void Duplicate(int index) override
    {
        if (index >= 0 && index < (int)cache.size()) {
            cache.push_back(cache[index]);
        }
    }

    // 行の高さを少し広げる
    virtual size_t GetCustomHeight(int index) override { return 24; }

    virtual void DoubleClick(int index) override
    {
        // ダブルクリック時の挙動（詳細ウィンドウを開くなど）を将来的に実装可能
    }

    virtual void CustomDraw(int index,
        ImDrawList             *draw_list,
        const ImRect           &rc,
        const ImRect           &legendRect,
        const ImRect           &clippingRect,
        const ImRect           &legendClippingRect) override
    {
    }
    virtual void CustomDrawCompact(
        int index, ImDrawList *draw_list, const ImRect &rc, const ImRect &clippingRect) override
    {
    }
};

// =============================================================================
// Window Class Implementation
// =============================================================================

AnimationSequencerWindow::AnimationSequencerWindow()
    : EditorWindow("Sequencer"), _expanded(true), _selectedEntry(-1), _firstFrame(0)
{
    _sequencerImpl = std::make_unique<AnimationSequenceImpl>();
    SetOpen(true);
}

AnimationSequencerWindow::~AnimationSequencerWindow() = default;

void AnimationSequencerWindow::DrawContents(EditorContext &context)
{
    // ---------------------------------------------------------
    // 1. ターゲット検証と準備
    // ---------------------------------------------------------
    if (!context.selectedEntity || *context.selectedEntity == 0) {
        ImGui::TextDisabled("Select an entity to edit animation.");
        return;
    }

    CCL::ECS::EntityID entity    = *context.selectedEntity;
    auto              *animator  = context.world->GetComponent<AnimatorComponent>(entity);
    auto              *modelComp = context.world->GetComponent<ModelComponent>(entity);

    if (!animator) {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Selected entity has no AnimatorComponent.");
        return;
    }

    // ---------------------------------------------------------
    // 2. 編集対象データの取得
    // ---------------------------------------------------------
    bool        targetFound     = false;
    std::string currentAnimName = "(None)";
    float       currentDuration = 1.0f;

    if (modelComp && modelComp->modelPtr) {
        Model         *model    = modelComp->modelPtr.get();
        ModelResource *resource = model->GetResourceMutable();

        if (resource) {
            auto &animations = resource->GetAnimationsMutable();
            int   idx        = animator->currentAnimIndex;

            if (idx >= 0 && idx < (int)animations.size()) {
                _sequencerImpl->targetEvents = &animations[idx].events;
                _sequencerImpl->UpdateDuration(animations[idx].secondsLength);

                currentAnimName = animations[idx].name;
                currentDuration = animations[idx].secondsLength;
                targetFound     = true;
            }
        }
    }

    if (!targetFound) {
        _sequencerImpl->targetEvents = nullptr;
    }

    // ---------------------------------------------------------
    // 3. ツールバー表示 (ボタン類)
    // ---------------------------------------------------------
    ImGui::Text("Anim: %s (%.2fs)", currentAnimName.c_str(), currentDuration);

    // 編集モード切替
    if (context.isAnimEditMode) {
        ImGui::Checkbox("Edit Mode (Pause & Preview)", context.isAnimEditMode);
    }

    ImGui::SameLine();
    ImGui::Text("|"); // シンプルに文字で縦線を描画
    ImGui::SameLine();

    // イベント追加ボタン群
    if (ImGui::Button("+ Attack")) {
        _sequencerImpl->Add(0); // HitBox
        _sequencerImpl->SyncToData();
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Sound")) {
        _sequencerImpl->Add(1); // Sound
        _sequencerImpl->SyncToData();
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Effect")) {
        _sequencerImpl->Add(2); // Effect
        _sequencerImpl->SyncToData();
    }

    ImGui::Separator();

// ---------------------------------------------------------
    // 4. シーケンサー描画
    // ---------------------------------------------------------
    _sequencerImpl->SyncFromData();

    // ★修正: BeginChild / EndChild を削除しました
    // これでマウスホイールのイベントが直接シーケンサーに届くようになります

    {
        // 時間管理
        int currentFrame = 0;
        if (context.animEditTime) {
            if (context.isAnimEditMode && *context.isAnimEditMode) {
                currentFrame = (int)(*context.animEditTime * 60.0f);
            }
            else {
                currentFrame = (int)(animator->currentTimer * 60.0f);
            }
        }

        _expanded = true;

        // シーケンサー本体
        bool changed = ImSequencer::Sequencer(_sequencerImpl.get(),
            &currentFrame,
            &_expanded,
            &_selectedEntry,
            &_firstFrame,
            ImSequencer::SEQUENCER_EDIT_STARTEND | ImSequencer::SEQUENCER_ADD |
                ImSequencer::SEQUENCER_DEL | ImSequencer::SEQUENCER_CHANGE_FRAME);

        if (changed) {
            _sequencerImpl->SyncToData();
        }

        // ---------------------------------------------------------
        // 5. プレビュー連動
        // ---------------------------------------------------------
        if (context.animEditTime && context.isAnimEditMode && *context.isAnimEditMode) {
            float newTime          = (float)currentFrame / 60.0f;
            *context.animEditTime  = newTime;
            animator->currentTimer = newTime;
        }
    }
  
}