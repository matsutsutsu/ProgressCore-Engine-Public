#include "AnimationSequencerWindow.h"
#include "Engine/GamePlay/Animation/AnimatorComponent.h"
#include "Engine/GamePlay/Graphics/Core/ModelComponent.h"
#include "ImSequencer.h"
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Graphics/Resource/ModelResource.h"
#include "Engine/GamePlay/Animation/AnimSequence.h" 
#include "Engine/GamePlay/Animation/AnimationSystem.h" // 手動更新用

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include <fstream>
#include <cereal/archives/json.hpp>

// =============================================================================
// データ管理クラス (ImSequencer <-> AnimSequence の仲介)
// =============================================================================
class AnimationSequenceImpl : public ImSequencer::SequenceInterface {
public:
    // UI表示用のキャッシュデータ (ブロック単位)
    struct EditorEvent {
        int frameStart;
        int frameEnd;
        int type;
    };

    std::vector<EditorEvent> cache;
    AnimSequence* targetSequence = nullptr; // 対象が台本に！

    int mFrameMin = 0;
    int mFrameMax = 60 * 5;

    // -------------------------------------------------------------------------
    // 同期処理: 台本(点) -> UI(ブロック) への変換
    // ---------------------------------------------------------
    void SyncFromData() {
        cache.clear();
        if (!targetSequence) return;

        int pendingHitBoxStart = -1;

        for (const auto& evt : targetSequence->events) {
            if (evt.eventName == "HitBox_Start") {
                pendingHitBoxStart = (int)(evt.time * 60.0f);
            }
            else if (evt.eventName == "HitBox_End") {
                if (pendingHitBoxStart != -1) {
                    EditorEvent item{ pendingHitBoxStart, (int)(evt.time * 60.0f), 0 };
                    cache.push_back(item);
                    pendingHitBoxStart = -1;
                }
            }
            else if (evt.eventName == "Play_Sound") {
                int frame = (int)(evt.time * 60.0f);
                cache.push_back({ frame, frame + 1, 1 });
            }
            else if (evt.eventName == "Play_Effect") {
                int frame = (int)(evt.time * 60.0f);
                cache.push_back({ frame, frame + 1, 2 });
            }
        }
    }

    // -------------------------------------------------------------------------
    // 同期処理: UI(ブロック) -> 台本(点) への変換と【自動ソート】
    // ---------------------------------------------------------
    void SyncToData() {
        if (!targetSequence) return;
        targetSequence->events.clear();

        for (const auto& item : cache) {
            float startSec = (float)item.frameStart / 60.0f;
            float endSec = (float)item.frameEnd / 60.0f;

            if (item.type == 0) { // HitBoxブロック -> StartとEndの2つに分解
                targetSequence->events.push_back({ startSec, "HitBox_Start" });
                targetSequence->events.push_back({ endSec, "HitBox_End" });
            }
            else if (item.type == 1) {
                targetSequence->events.push_back({ startSec, "Play_Sound" });
            }
            else if (item.type == 2) {
                targetSequence->events.push_back({ startSec, "Play_Effect" });
            }
        }

        // ★絶対条件: O(1)アクセスのため、時間を基準に昇順ソートする
        std::sort(targetSequence->events.begin(), targetSequence->events.end(),
            [](const AnimNotifyEvent& a, const AnimNotifyEvent& b) {
                return a.time < b.time;
            });
    }

    void UpdateDuration(float durationSeconds) {
        mFrameMax = (int)(durationSeconds * 60.0f * 1.1f);
        if (mFrameMax < 60) mFrameMax = 60;
    }

    // --- ImSequencer Interface ---
    virtual int GetFrameMin() const override { return mFrameMin; }
    virtual int GetFrameMax() const override { return mFrameMax; }
    virtual int GetItemCount() const override { return (int)cache.size(); }
    virtual int GetItemTypeCount() const override { return 3; }

    virtual const char* GetItemTypeName(int typeIndex) const override {
        switch (typeIndex) {
        case 0: return "HitBox (Attack)";
        case 1: return "Sound (SE)";
        case 2: return "Effect (VFX)";
        default: return "Unknown";
        }
    }

    virtual const char* GetItemLabel(int index) const override {
        static char temp[64];
        if (index < 0 || index >= (int)cache.size()) return "";
        const auto& item = cache[index];
        switch (item.type) {
        case 0: sprintf_s(temp, "[%d] Attack", index); break;
        case 1: sprintf_s(temp, "[%d] Sound", index); break;
        case 2: sprintf_s(temp, "[%d] Effect", index); break;
        default: sprintf_s(temp, "[%d] Event", index); break;
        }
        return temp;
    }

    virtual void Get(int index, int** start, int** end, int* type, unsigned int* color) override {
        if (index < 0 || index >= (int)cache.size()) return;
        EditorEvent& item = cache[index];
        if (start) *start = &item.frameStart;
        if (end) *end = &item.frameEnd;
        if (type) *type = item.type;
        if (color) {
            switch (item.type) {
            case 0: *color = 0xFF5050D0; break; // 赤
            case 1: *color = 0xFFD08050; break; // 青
            case 2: *color = 0xFF50D050; break; // 緑
            default: *color = 0xFFFFFFFF; break;
            }
        }
    }

    virtual void Add(int type) override {
        EditorEvent item;
        item.type = type;
        item.frameStart = 0;
        item.frameEnd = (type == 0) ? 30 : 1; // HitBoxのみ幅を持たせる
        cache.push_back(item);
    }

    virtual void Del(int index) override {
        if (index >= 0 && index < (int)cache.size()) cache.erase(cache.begin() + index);
    }
    virtual void Duplicate(int index) override {
        if (index >= 0 && index < (int)cache.size()) cache.push_back(cache[index]);
    }
    virtual size_t GetCustomHeight(int index) override { return 24; }
    virtual void DoubleClick(int index) override {}
    virtual void CustomDraw(int, ImDrawList*, const ImRect&, const ImRect&, const ImRect&, const ImRect&) override {}
    virtual void CustomDrawCompact(int, ImDrawList*, const ImRect&, const ImRect&) override {}
};

// =============================================================================
// Window Class Implementation
// =============================================================================
static AnimSequence g_EditingSequence;
static char g_SavePath[256] = "Assets/Animations/Player_Attack.json";

AnimationSequencerWindow::AnimationSequencerWindow()
    : EditorWindow("Sequencer"), _expanded(true), _selectedEntry(-1), _firstFrame(0)
{
    _sequencerImpl = std::make_unique<AnimationSequenceImpl>();
    SetOpen(true);
}

AnimationSequencerWindow::~AnimationSequencerWindow() = default;

void AnimationSequencerWindow::DrawContents(EditorContext& context)
{
    if (context.selectedEntity == CCL::ECS::InvalidEntityID || context.selectedEntity == 0) {
        ImGui::TextDisabled("Select an entity to edit animation.");
        return;
    }

    CCL::ECS::EntityID entity = context.selectedEntity;
    auto* animator = context.world->GetComponent<AnimatorComponent>(entity);
    auto* modelComp = context.world->GetComponent<ModelComponent>(entity);

    if (!animator || !modelComp || !modelComp->modelPtr) {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Entity needs Animator and Model components.");
        return;
    }

    Model* model = modelComp->modelPtr.get();
    ModelResource* resource = model->GetResourceMutable();
    if (!resource) return;

    // ---------------------------------------------------------
    // JSON保存・読み込みのUI
    // ---------------------------------------------------------
    ImGui::InputText("Save Path", g_SavePath, sizeof(g_SavePath));

    if (ImGui::Button("Save JSON")) {
        std::ofstream os(g_SavePath);
        if (os.is_open()) {
            cereal::JSONOutputArchive archive(os);
            archive(cereal::make_nvp("AnimSequence", g_EditingSequence));
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Load JSON")) {
        std::ifstream is(g_SavePath);
        if (is.is_open()) {
            cereal::JSONInputArchive archive(is);
            archive(cereal::make_nvp("AnimSequence", g_EditingSequence));
            _sequencerImpl->SyncFromData();
        }
    }

    ImGui::Separator();

    // ---------------------------------------------------------
    // ターゲットの関連付け
    // ---------------------------------------------------------
    auto& animations = resource->GetAnimationsMutable();
    if (animations.empty()) {
        ImGui::TextDisabled("No animations found in the model.");
        return;
    }

    static int s_SelectedAnimIndex = 0;
    if (s_SelectedAnimIndex >= (int)animations.size()) {
        s_SelectedAnimIndex = 0;
    }

    if (ImGui::BeginCombo("Target Animation", animations[s_SelectedAnimIndex].name.c_str())) {
        for (int i = 0; i < (int)animations.size(); ++i) {
            bool isSelected = (s_SelectedAnimIndex == i);
            if (ImGui::Selectable(animations[i].name.c_str(), isSelected)) {
                s_SelectedAnimIndex = i;
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    int idx = s_SelectedAnimIndex;

    g_EditingSequence.targetAnimName = animations[idx].name;
    g_EditingSequence.duration = animations[idx].secondsLength;

    _sequencerImpl->targetSequence = &g_EditingSequence;
    _sequencerImpl->UpdateDuration(animations[idx].secondsLength);

    if (context.isAnimEditMode) {
        animator->currentSequence = &g_EditingSequence;
    }

    // ---------------------------------------------------------
    // ツールバー表示 (ボタン類)
    // ---------------------------------------------------------
    ImGui::Text("Anim: %s (%.2fs)", g_EditingSequence.targetAnimName.c_str(), g_EditingSequence.duration);
    ImGui::Checkbox("Edit Mode (Pause & Preview)", &context.isAnimEditMode);

    ImGui::SameLine(); ImGui::Text("|"); ImGui::SameLine();

    if (ImGui::Button("+ Attack")) { _sequencerImpl->Add(0); _sequencerImpl->SyncToData(); }
    ImGui::SameLine();
    if (ImGui::Button("+ Sound")) { _sequencerImpl->Add(1); _sequencerImpl->SyncToData(); }
    ImGui::SameLine();
    if (ImGui::Button("+ Effect")) { _sequencerImpl->Add(2); _sequencerImpl->SyncToData(); }

    ImGui::Separator();


    // ---------------------------------------------------------
    // シーケンサー描画
    // ---------------------------------------------------------
    if (_sequencerImpl->targetSequence) {
        _sequencerImpl->SyncFromData();

        int currentFrame = 0;
        if (context.isAnimEditMode) {
            currentFrame = (int)(context.animEditTime * 60.0f);
        } else {
            currentFrame = (int)(animator->currentTimer * 60.0f);
        }

        int prevFrame = currentFrame;

        _expanded = true;
        bool changed = ImSequencer::Sequencer(_sequencerImpl.get(), &currentFrame, &_expanded, &_selectedEntry, &_firstFrame,
            ImSequencer::SEQUENCER_EDIT_STARTEND | ImSequencer::SEQUENCER_ADD |
            ImSequencer::SEQUENCER_DEL | ImSequencer::SEQUENCER_CHANGE_FRAME);

        if (changed) {
            _sequencerImpl->SyncToData(); // 編集されたらデータに変換＆ソート
        }

        // =========================================================
        // ★手回し映写機の連動: シークバーを動かした瞬間に骨格を更新
        // =========================================================
        if (currentFrame != prevFrame || changed) {
            float newTime = (float)currentFrame / 60.0f;
            
            context.animEditTime = newTime;
            animator->currentTimer = newTime;

            if (context.systemManager) {
                auto* animSystem = context.systemManager->GetSystem<AnimationSystem>();
                if (animSystem) {
                    animSystem->UpdateManual(entity, newTime);
                }
            }
        }
        else if (context.isAnimEditMode) {
            context.animEditTime = (float)currentFrame / 60.0f;
        }
    }
}
