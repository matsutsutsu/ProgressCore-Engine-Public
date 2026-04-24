#include "AnimationSequencerWindow.h"
#include "Engine/GamePlay/Animation/AnimatorComponent.h"
#include "Engine/GamePlay/Graphics/Core/ModelComponent.h"
#include "ImSequencer.h"
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Graphics/Resource/ModelResource.h"
#include "Engine/GamePlay/Animation/Data/AnimSequence.h" 
#include "Engine/GamePlay/Animation/AnimationSystem.h" 

#include "Engine/Platform/Dialog.h"
#include "Engine/Platform/Logger.h"
#include "Engine/Graphics/Core/Graphics.h"
#include <filesystem>

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
    struct EditorEvent {
        int frameStart;
        int frameEnd;
        int type;
        char stringParam[256];
    };

    std::vector<EditorEvent> cache;
    AnimSequence* targetSequence = nullptr;

    int mFrameMin = 0;
    int mFrameMax = 60 * 5;

    // 台本(点) -> UI(ブロック) への変換
    void SyncFromData() {
        cache.clear();
        if (!targetSequence) return;

        int pendingHitBoxStart = -1;
        std::string pendingHitBoxParam = "";

        for (const auto& evt : targetSequence->events) {
            if (evt.eventName == "HitBox_Start") {
                pendingHitBoxStart = (int)(evt.time * 60.0f);
                pendingHitBoxParam = evt.stringParam;
            }
            else if (evt.eventName == "HitBox_End") {
                if (pendingHitBoxStart != -1) {
                    EditorEvent item{ pendingHitBoxStart, (int)(evt.time * 60.0f), 0 };
                    strcpy_s(item.stringParam, sizeof(item.stringParam), pendingHitBoxParam.c_str());
                    cache.push_back(item);
                    pendingHitBoxStart = -1;
                }
            }
            else if (evt.eventName == "Play_Sound") {
                int frame = (int)(evt.time * 60.0f);
                // ★修正: 音やエフェクトも10フレーム分の幅を持たせ、マウスで掴みやすくする
                EditorEvent item{ frame, frame + 10, 1 };
                strcpy_s(item.stringParam, sizeof(item.stringParam), evt.stringParam.c_str());
                cache.push_back(item);
            }
            else if (evt.eventName == "Play_Effect") {
                int frame = (int)(evt.time * 60.0f);
                EditorEvent item{ frame, frame + 10, 2 };
                strcpy_s(item.stringParam, sizeof(item.stringParam), evt.stringParam.c_str());
                cache.push_back(item);
            }
        }
    }

    // UI(ブロック) -> 台本(点) への変換と自動ソート
    void SyncToData() {
        if (!targetSequence) return;
        targetSequence->events.clear();

        for (const auto& item : cache) {
            float startSec = (float)item.frameStart / 60.0f;
            float endSec = (float)item.frameEnd / 60.0f;

            if (item.type == 0) {
                targetSequence->events.push_back({ startSec, "HitBox_Start", item.stringParam });
                targetSequence->events.push_back({ endSec, "HitBox_End", item.stringParam });
            }
            else if (item.type == 1) {
                targetSequence->events.push_back({ startSec, "Play_Sound", item.stringParam });
            }
            else if (item.type == 2) {
                targetSequence->events.push_back({ startSec, "Play_Effect", item.stringParam });
            }
        }

        std::sort(targetSequence->events.begin(), targetSequence->events.end(),
            [](const AnimNotifyEvent& a, const AnimNotifyEvent& b) {
                return a.time < b.time;
            });
    }

    void UpdateDuration(float durationSeconds) {
        mFrameMax = (int)(durationSeconds * 60.0f * 1.1f);
        if (mFrameMax < 60) mFrameMax = 60;
    }

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
        static char temp[128];
        if (index < 0 || index >= (int)cache.size()) return "";
        const auto& item = cache[index];

        const char* name = (item.stringParam[0] != '\0') ? item.stringParam : "Empty";

        switch (item.type) {
        case 0: sprintf_s(temp, "[%d] Attack: %s", index, name); break;
        case 1: sprintf_s(temp, "[%d] Sound: %s", index, name); break;
        case 2: sprintf_s(temp, "[%d] Effect: %s", index, name); break;
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
            case 0: *color = 0xFF5050D0; break;
            case 1: *color = 0xFFD08050; break;
            case 2: *color = 0xFF50D050; break;
            default: *color = 0xFFFFFFFF; break;
            }
        }
    }

    virtual void Add(int type) override {
        EditorEvent item;
        item.type = type;
        item.frameStart = 0;
        item.frameEnd = (type == 0) ? 30 : 10;
        memset(item.stringParam, 0, sizeof(item.stringParam));
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
static char g_SavePath[256] = "Assets/Animations/NewSequence.json";
static int s_SelectedAnimIndex = -1; // ターゲットアニメーションのコンボボックス用

AnimationSequencerWindow::AnimationSequencerWindow()
    : EditorWindow("Sequencer"), _expanded(true), _selectedEntry(-1), _firstFrame(0)
{
    _sequencerImpl = std::make_unique<AnimationSequenceImpl>();
    SetVisible(true);
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

    if (!animator || !modelComp || !modelComp->GetModel()) {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Entity needs Animator and Model components.");
        return;
    }

    Model* model = modelComp->GetModel();
    ModelResource* resource = model->GetResourceMutable();
    if (!resource) return;

    auto& animations = resource->GetAnimationsMutable();
    if (animations.empty()) {
        ImGui::TextDisabled("No animations found in the model.");
        return;
    }

    // ---------------------------------------------------------
    // JSON保存・読み込みのUI
    // ---------------------------------------------------------
    if (ImGui::Button("Save JSON...")) {
        char filename[MAX_PATH] = {};
        if (Dialog::SaveFileName(filename, MAX_PATH, "JSON Files\0*.json\0", "Save Anim Sequence", "json", Graphics::Instance().GetWindowHandle()) == DialogResult::OK) {
            namespace fs = std::filesystem;
            std::error_code ec;
            fs::path relPath = fs::relative(filename, fs::current_path(), ec);
            std::string finalPath = (!ec && !relPath.empty()) ? relPath.generic_string() : filename;

            strcpy_s(g_SavePath, sizeof(g_SavePath), finalPath.c_str());

            // ★保存直前に最新のUI状態をデータに書き込む
            _sequencerImpl->SyncToData();

            std::ofstream os(finalPath);
            if (os.is_open()) {
                cereal::JSONOutputArchive archive(os);
                archive(cereal::make_nvp("AnimSequence", g_EditingSequence));
                CCL_LOG_INFO(LogCategory::Editor, "Sequence Saved: %s", finalPath.c_str());
            }
        }
    }
    ImGui::SameLine();

    if (ImGui::Button("Load JSON...")) {
        char filename[MAX_PATH] = {};
        if (Dialog::OpenFileName(filename, MAX_PATH, "JSON Files\0*.json\0", "Load Anim Sequence", Graphics::Instance().GetWindowHandle()) == DialogResult::OK) {
            namespace fs = std::filesystem;
            std::error_code ec;
            fs::path relPath = fs::relative(filename, fs::current_path(), ec);
            std::string finalPath = (!ec && !relPath.empty()) ? relPath.generic_string() : filename;

            strcpy_s(g_SavePath, sizeof(g_SavePath), finalPath.c_str());

            std::ifstream is(finalPath);
            if (is.is_open()) {
                cereal::JSONInputArchive archive(is);
                archive(cereal::make_nvp("AnimSequence", g_EditingSequence));

                // 読み込んだデータに合わせてターゲットコンボボックスを更新
                for (int i = 0; i < (int)animations.size(); ++i) {
                    if (animations[i].name == g_EditingSequence.targetAnimName) {
                        s_SelectedAnimIndex = i;
                        break;
                    }
                }

                _sequencerImpl->targetSequence = &g_EditingSequence;
                _sequencerImpl->UpdateDuration(g_EditingSequence.duration);
                _sequencerImpl->SyncFromData(); // UIを更新
                _selectedEntry = -1;
                CCL_LOG_INFO(LogCategory::Editor, "Sequence Loaded: %s", finalPath.c_str());
            }
        }
    }

    ImGui::SameLine();
    ImGui::TextDisabled("File: %s", g_SavePath[0] == '\0' ? "None" : g_SavePath);
    ImGui::Separator();

    // ★追加: ノードエディタで使う「Sequence Name」の編集
    char seqNameBuf[128] = {};
    strcpy_s(seqNameBuf, sizeof(seqNameBuf), g_EditingSequence.sequenceName.c_str());
    if (ImGui::InputText("Sequence Name", seqNameBuf, sizeof(seqNameBuf))) {
        g_EditingSequence.sequenceName = seqNameBuf;
    }
    ImGui::Separator();

    // ---------------------------------------------------------
    // ターゲットの関連付けと初期化
    // ---------------------------------------------------------
    if (s_SelectedAnimIndex < 0 || s_SelectedAnimIndex >= (int)animations.size()) {
        s_SelectedAnimIndex = 0; // フェールセーフ
    }

    static int prevSelectedIndex = -2;
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

    // ベースアニメーションが変更された時だけデータをリセットして同期する
    if (prevSelectedIndex != s_SelectedAnimIndex) {
        prevSelectedIndex = s_SelectedAnimIndex;
        int idx = s_SelectedAnimIndex;

        g_EditingSequence.targetAnimName = animations[idx].name;
        g_EditingSequence.duration = animations[idx].secondsLength;

        _sequencerImpl->targetSequence = &g_EditingSequence;
        _sequencerImpl->UpdateDuration(animations[idx].secondsLength);

        // ここでのみSyncFromDataを呼ぶことで、ドラッグ中の破壊を防ぐ
        _sequencerImpl->SyncFromData();
    }

    if (context.isAnimEditMode) {
        animator->currentSequence = &g_EditingSequence;
    }

    // ---------------------------------------------------------
    // ツールバー表示 (ボタン類)
    // ---------------------------------------------------------
    ImGui::Text("Duration: %.2fs", g_EditingSequence.duration);
    ImGui::Checkbox("Edit Mode (Pause & Preview)", &context.isAnimEditMode);
    ImGui::SameLine(); ImGui::Text("|"); ImGui::SameLine();

    int currentFrame = 0;
    if (context.isAnimEditMode) {
        currentFrame = (int)(context.animEditTime * 60.0f);
    }
    else {
        currentFrame = (int)(animator->currentTimer * 60.0f);
    }
    int prevFrame = currentFrame;

    // ====================================================================
    // イベントを追加し、現在再生している位置(Playhead)に配置する
    // ====================================================================
    auto AddAndSelectEvent = [&](int type) {
        _sequencerImpl->Add(type);

        auto& newItem = _sequencerImpl->cache.back();
        newItem.frameStart = currentFrame;
        newItem.frameEnd = currentFrame + ((type == 0) ? 30 : 10);

        _sequencerImpl->SyncToData();
        _selectedEntry = (int)_sequencerImpl->cache.size() - 1;
        };

    if (ImGui::Button("+ Attack")) { AddAndSelectEvent(0); }
    ImGui::SameLine();
    if (ImGui::Button("+ Sound")) { AddAndSelectEvent(1); }
    ImGui::SameLine();
    if (ImGui::Button("+ Effect")) { AddAndSelectEvent(2); }

    ImGui::Separator();

    // ---------------------------------------------------------
    // シーケンサー描画
    // ---------------------------------------------------------
    if (_sequencerImpl->targetSequence) {
        _expanded = true;

        // ★修正: SyncFromData()を毎フレーム呼ぶのをやめたため、ドラッグが安定します
        bool changed = ImSequencer::Sequencer(_sequencerImpl.get(), &currentFrame, &_expanded, &_selectedEntry, &_firstFrame,
            ImSequencer::SEQUENCER_EDIT_STARTEND | ImSequencer::SEQUENCER_ADD |
            ImSequencer::SEQUENCER_DEL | ImSequencer::SEQUENCER_CHANGE_FRAME);

        // ====================================================================
        // 　選択されたブロックのインスペクタ (プロパティ編集UI)
        // ====================================================================
        if (_selectedEntry >= 0 && _selectedEntry < (int)_sequencerImpl->cache.size()) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Selected Event Properties");

            auto& selectedItem = _sequencerImpl->cache[_selectedEntry];

            if (selectedItem.type == 1 || selectedItem.type == 2 || selectedItem.type == 0) {
                const char* label = (selectedItem.type == 1) ? "Sound File" : ((selectedItem.type == 2) ? "Effect Prefab" : "Damage Info");
                const char* filter = (selectedItem.type == 1) ? "Audio Files\0*.wav;*.mp3;*.ogg\0All Files\0*.*\0" : "Prefab Files\0*.json\0All Files\0*.*\0";

                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 100.0f);
                if (ImGui::InputText(label, selectedItem.stringParam, sizeof(selectedItem.stringParam))) {
                    changed = true;
                }

                if (selectedItem.type == 1 || selectedItem.type == 2) {
                    ImGui::SameLine();
                    if (ImGui::Button("Browse...##ParamBrowser")) {
                        char filename[256] = {};
                        if (Dialog::OpenFileName(filename, 256, filter, "Select File", Graphics::Instance().GetWindowHandle()) == DialogResult::OK) {
                            namespace fs = std::filesystem;
                            std::error_code ec;
                            fs::path relPath = fs::relative(filename, fs::current_path(), ec);
                            std::string finalPath = (!ec && !relPath.empty()) ? relPath.generic_string() : filename;

                            strcpy_s(selectedItem.stringParam, sizeof(selectedItem.stringParam), finalPath.c_str());
                            changed = true;
                        }
                    }
                }
            }
        }
        ImGui::Separator();

        // ====================================================================
        // 　変更があったら台本に書き戻す
        // ====================================================================
        if (changed) {
            // ★修正: SyncFromData()を呼ばないことで、配列の再構築を防ぐ
            _sequencerImpl->SyncToData();
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