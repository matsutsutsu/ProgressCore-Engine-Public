// ===================================================================================
// ファイル: AnimationSequencerWindow.cpp
// 役割       : AnimSequence 編集ウィンドウの実装。
// 責務       : ImSequencer によるイベント編集、CurveEditorWidget による速度/RootMotion
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
// ===================================================================================
// AnimationSequencerWindow メンバ関数群の実装
// ===================================================================================
AnimationSequencerWindow::AnimationSequencerWindow()
    : EditorWindow("Sequencer")
{
    _sequencerImpl = std::make_unique<AnimationSequenceImpl>();

    // 初期状態バインド
    _sequencerImpl->SetSequence(&_editingSequence);
    _sequencerImpl->SyncFrameCache();

    SetVisible(true);
}

AnimationSequencerWindow::~AnimationSequencerWindow() = default;

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
    DrawCurvePane(context.animEditTime);

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

            // ルートモーションプレビューで動かした位置を、切替前のエンティティに対して復元する
            // （_lastSelectedEntity はまだ更新前＝古いエンティティを指している）
            if (_rootMotionActive)
            {
                if (context.world)
                {
                    if (auto *oldTrans = context.world->GetComponent<TransformComponent>(_lastSelectedEntity))
                        oldTrans->SetPosition(_rootMotionBasePos);
                }
                _rootMotionActive = false;
            }
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

        // ルートモーションプレビューで動かした位置を元に戻す
        if (_rootMotionActive)
        {
            if (context.world)
            {
                if (auto *trans = context.world->GetComponent<TransformComponent>(context.selectedEntity))
                    trans->SetPosition(_rootMotionBasePos);
            }
            _rootMotionActive = false;
        }
    }
}

void AnimationSequencerWindow::DrawRootMotionPreview(EditorContext &context, AnimatorComponent * /*animator*/)
{
    if (!context.isAnimEditMode || !context.world) return;

    TransformComponent *trans = context.world->GetComponent<TransformComponent>(context.selectedEntity);
    if (!trans) return;

    const CurveData &rmc = _editingSequence.rootMotionCurve;
    if (rmc.empty())
    {
        // カーブが無い（または全キー削除された）→ プレビュー駆動を終了し、
        // 次にキーが追加された瞬間の位置を新しい基準点として扱う。
        _rootMotionActive = false;
        return;
    }

    const float dur      = _editingSequence.duration;
    const float progress = (dur > 0.0f) ? std::clamp(context.animEditTime / dur, 0.0f, 1.0f) : 0.0f;

    if (!_rootMotionActive)
    {
        // このフレームで初めてカーブ駆動プレビューが有効になった → 現在位置を基準点として記録する。
        // 以後は毎フレーム「基準点 + 前方 * (curve(progress) - curve(0))」で絶対位置を
        // 再計算するため、スクラブや巻き戻しをしても誤差が蓄積しない。
        _rootMotionBasePos = trans->position;
        _rootMotionActive  = true;
    }

    // ワールド前方ベクトル（worldMatrixのZ軸列。回転はキャッシュ済みのものを使う）
    DirectX::XMFLOAT3 fwd = trans->GetForward();

    const float dist = rmc.Evaluate(progress) - rmc.Evaluate(0.0f);

    DirectX::XMFLOAT3 newPos = {
        _rootMotionBasePos.x + fwd.x * dist,
        _rootMotionBasePos.y + fwd.y * dist,
        _rootMotionBasePos.z + fwd.z * dist,
    };
    trans->SetPosition(newPos);

    // worldMatrix は本来 TransformUpdateSystem が計算するキャッシュだが、
    // このフレーム内で直後に model->UpdateTransform() がそれを読みに行くため、ここで即時再計算する。
    // ※ 親を持つエンティティでは親行列を考慮していないため位置がずれる（プレビュー専用の簡易実装）。
    trans->UpdateMatrix();
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
            const float progress0 = (dur > 0.0f) ? std::clamp(context.animEditTime / dur, 0.0f, 1.0f) : 0.0f;
            const float speedMult = _editingSequence.speedCurve.empty()
                                        ? 1.0f
                                        : _editingSequence.speedCurve.Evaluate(progress0);
            context.animEditTime += ImGui::GetIO().DeltaTime * animator->playbackSpeed * speedMult;
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

    // ルートモーションカーブのプレビュー適用（進捗→累積前進距離を Transform に反映）
    // ※ 下の UpdateTransform はキャッシュ済み worldMatrix を読むだけなので、
    //    表示への反映を最短にするためここで先に位置を確定させる。
    DrawRootMotionPreview(context, animator);

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
                isCombo ? "コンボ受付窓 (Combo Window)" : "キャンセル窓 (Cancel Window)");
            ImGui::Separator();

            ImGui::Text("許可するキャンセルアクション:");

            // ① 現在選択されているフラグの名前を繋げてプレビュー文字列を作る
            std::string preview = "";
            if (e.cancelMask == 0)
            {
                preview = "未設定 (None)";
            }
            else
            {
                for (const auto &def : GetCancelFlagDefs())
                {
                    if (e.cancelMask & def.flag)
                    {
                        if (!preview.empty()) preview += ", ";
                        preview += def.name;
                    }
                }
            }

            // ② ドロップダウン（コンボボックス）の描画
            ImGui::SetNextItemWidth(-1); // 横幅いっぱいに広げる
            if (ImGui::BeginCombo("##CancelMaskCombo", preview.c_str()))
            {
                for (const auto &def : GetCancelFlagDefs())
                {
                    bool active = (e.cancelMask & def.flag) != 0;

                    // コンボボックス内にチェックボックスを配置
                    // ※ ImGuiのCheckboxはクリックしてもコンボボックスが勝手に閉じないという利点がある
                    if (ImGui::Checkbox(def.name, &active))
                    {
                        if (active)
                            e.cancelMask |= def.flag;
                        else
                            e.cancelMask &= ~def.flag;
                        ch = true; // 変更を検知
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Active [%.2fs - %.2fs]", e.startTime, e.endTime);
            break;
        }
        default:
            break;
        }

        if (ch)
            _sequencerImpl->SyncFrameCache();
    }
}

void AnimationSequencerWindow::DrawCurvePane(float animTime)
{
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(1, 0.6f, 0.2f, 1), "アニメーションカーブ (横軸 = 進捗 0〜1)");

    static bool showSpeed = true, showRoot = true;
    ImGui::Checkbox("速度##cv", &showSpeed);
    ImGui::SameLine();
    ImGui::Checkbox("ルートモーション##cv", &showRoot);
    ImGui::SameLine();
    if (ImGui::Button("全体表示##cv")) _fitCurveView = true;
    ImGui::SameLine();
    ImGui::TextDisabled("[ダブルクリック=キー追加 / 右orホイール=パン&ズーム / Alt+ハンドル=接線分離]");

    _editingSequence.speedCurve.visible      = showSpeed;
    _editingSequence.rootMotionCurve.visible = showRoot;

    const float dur      = _editingSequence.duration;
    const float progress = (dur > 0.0f) ? std::clamp(animTime / dur, 0.0f, 1.0f) : 0.0f;

    // 編集対象の切り替え（選択が毎フレーム消えないよう、変化時だけ SetActiveCurve）
    int active = _curveWidget.ActiveCurveIndex();
    int prev   = active;
    ImGui::RadioButton("速度を編集", &active, 0);
    ImGui::SameLine();
    ImGui::RadioButton("ルートモーションを編集", &active, 1);
    if (active != prev) _curveWidget.SetActiveCurve(active);

    std::vector<CurveData *> targets = {&_editingSequence.speedCurve, &_editingSequence.rootMotionCurve};

    CurveEditorWidget::Options opt;
    opt.height        = 240.0f;
    opt.showInspector = true;
    opt.showPlayhead  = true;
    opt.playheadX     = progress;
    opt.fitRequested  = _fitCurveView;
    _fitCurveView     = false;

    _curveWidget.Draw("##seqCurves", targets, opt);

    // 現在の再生位置での評価値を即読み出し（数値でも確認できる）
    const float sp = _editingSequence.speedCurve.empty() ? 1.0f : _editingSequence.speedCurve.Evaluate(progress);
    const float rm = _editingSequence.rootMotionCurve.empty() ? 0.0f : _editingSequence.rootMotionCurve.Evaluate(progress);
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f),
        "進捗 %.2f  |  速度倍率 %.3f  |  累積前進 %.3f m", progress, sp, rm);

    if (active == 1 && !_editingSequence.rootMotionCurve.empty())
    {
        ImGui::TextDisabled("※ 上のシーンビューでキャラが実際に前進する量として確認できます。");
    }
}