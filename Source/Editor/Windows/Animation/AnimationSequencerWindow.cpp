#include "Editor/Windows/Animation/AnimationSequencerWindow.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Animation/Systems/AnimationSystem.h"
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Graphics/Resource/ModelResource.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Core/Components/NameComponent.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Animation/Components/CombatRosterComponent.h"
#include "Engine/Physics/JoltIntegration/PhysicsLayers.h"
#include "Engine/Graphics/Renderer/ShapeRenderer.h"
#include "Engine/Audio/IAudioAPI.h"

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
#include <filesystem>
#include <memory>
#include <vector>

// 秒とフレーム（60fps）の変換
inline int SecToFrame(float sec)
{
    return (int)(sec * 60.0f);
}



inline float FrameToSec(int frame)
{
    return (float)frame / 60.0f;
}



// Effect イベントで選べる VFX プレハブの置き場所（VFX::Play と同じく拡張子なしのパスで保存する）
constexpr const char *kVfxPrefabDir = "Assets/Prefabs/Particles";

// ---------------------------------------------------------------------------
// ImSequencer へ AnimSequence のイベントを見せる橋渡し（pImpl）
// ---------------------------------------------------------------------------
class AnimationSequenceImpl : public ImSequencer::SequenceInterface
{
public:
    AnimSequence *m_seq = nullptr; // 編集中のシーケンス（所有しない）

    // ImSequencer が直接書き換えるフレームの表。[0] はマスタートラック（長さ）、[1] 以降がイベント
    mutable std::vector<std::pair<int, int>> m_frameCacheMap;

    AnimationSequenceImpl() = default;

    void SetSequence(AnimSequence *seq)
    {
        m_seq = seq;
    }

    // シーケンスの秒を、フレームの表へ写す
    void SyncFrameCache() const
    {
        if (!m_seq)
            return;

        // 常に「イベント数 + 1 (マスタートラック)」を確保する
        m_frameCacheMap.resize(m_seq->events.size() + 1);

        // [0] マスタートラック: 開始は常に0、終了は現在のアセット長(duration)
        m_frameCacheMap[0].first  = 0;
        m_frameCacheMap[0].second = SecToFrame(m_seq->duration);

        // [1] 以降: 各イベントの開始・終了
        for (size_t i = 0; i < m_seq->events.size(); ++i)
        {
            m_frameCacheMap[i + 1].first  = SecToFrame(m_seq->events[i].startTime);
            m_frameCacheMap[i + 1].second = SecToFrame(m_seq->events[i].endTime);
        }
    }

    // UI 上でドラッグされたフレームを、シーケンスの秒へ書き戻す
    void FlushFrameCache() const
    {
        if (!m_seq || m_frameCacheMap.empty())
            return;

        // マスタートラックはドラッグさせず、アセット本来の長さに固定する
        m_frameCacheMap[0].first  = 0;
        m_frameCacheMap[0].second = SecToFrame(m_seq->duration);

        // イベントの時間を同期
        for (size_t i = 0; i < m_seq->events.size(); ++i)
        {
            if (i + 1 < m_frameCacheMap.size())
            {
                m_seq->events[i].startTime = FrameToSec(m_frameCacheMap[i + 1].first);
                m_seq->events[i].endTime   = FrameToSec(m_frameCacheMap[i + 1].second);
            }
        }
    }

    virtual int GetItemCount() const override
    {
        if (!m_seq)
            return 0;
        // マスタートラックが必ず存在するため、常に 1 以上になる
        return (int)m_seq->events.size() + 1;
    }

    virtual int GetFrameMin() const override
    {
        return 0;
    }
    virtual int GetFrameMax() const override
    {
        if (!m_seq)
            return 100;
        int maxFrames = SecToFrame(m_seq->duration);
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
            "SpawnHitbox", "Trail", "ComboWindow", "CancelWindow", "Particle"};
        if (typeIndex < 0 || typeIndex >= (int)AnimEventType::Count)
            return "Unknown";
        return names[typeIndex];
    }

    virtual const char *GetItemLabel(int index) const override
    {
        if (!m_seq)
            return "";
        if (index == 0)
            return "Master (Duration)"; // 0番目は常にマスタートラック

        int evIdx = index - 1;
        if (evIdx < 0 || evIdx >= (int)m_seq->events.size())
            return "";

        static char buf[128];
        const auto &ev = m_seq->events[evIdx];
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
        case AnimEventType::Particle:
            snprintf(buf, sizeof(buf), "Particle: %s", ev.stringParam.c_str());
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
        if (!m_seq)
            return;

        // ここで SyncFrameCache() を呼ばない
        //   Get() は毎フレーム呼ばれるので、ここで作り直すと、ドラッグ中の値が毎フレーム戻されてドラッグできない

        if (index < 0 || index >= (int)m_frameCacheMap.size())
            return;

        if (start)
            *start = &m_frameCacheMap[index].first;
        if (end)
            *end = &m_frameCacheMap[index].second;

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
                *type = (int)m_seq->events[evIdx].type;
            if (color)
            {
                static const unsigned int colors[] = {
                    0xFF323296, 0xFF329632, 0xFF969632, 0xFF963296,
                    0xFF1010A0, 0xFF96A032, 0xFF32A096, 0xFF505050,
                    0xFFC86432}; // Particle: 橙(まとい)
                *color = colors[(int)m_seq->events[evIdx].type % 9];
            }
        }
    }

    virtual void Add(int type) override
    {
        if (!m_seq)
            return;
        AnimNotifyEvent ev;
        ev.type      = (AnimEventType)type;
        ev.startTime = 0.0f;
        ev.endTime   = (std::min)(0.5f, m_seq->duration);

        if (type == (int)AnimEventType::Effect)
            ev.stringParam = std::string(kVfxPrefabDir) + "/VFX_HitSpark";
        if (type == (int)AnimEventType::HitBox)
            ev.stringParam = "Weapon_R";
        if (type == (int)AnimEventType::Trail)
            ev.stringParam = "Weapon_R";
        if (type == (int)AnimEventType::Particle)
            ev.stringParam = "Weapon_R";

        m_seq->events.push_back(ev);
        SyncFrameCache();
    }

    virtual void Del(int index) override
    {
        if (!m_seq || index == 0)
            return; // マスターは削除不可
        int evIdx = index - 1;
        if (evIdx >= 0 && evIdx < (int)m_seq->events.size())
        {
            m_seq->events.erase(m_seq->events.begin() + evIdx);
            SyncFrameCache();
        }
    }

    virtual void Duplicate(int index) override
    {
        if (!m_seq || index == 0)
            return; // マスターは複製不可
        int evIdx = index - 1;
        if (evIdx >= 0 && evIdx < (int)m_seq->events.size())
        {
            m_seq->events.push_back(m_seq->events[evIdx]);
            SyncFrameCache();
        }
    }
};



// ---------------------------------------------------------------------------
// AnimationSequencerWindow
// ---------------------------------------------------------------------------
AnimationSequencerWindow::AnimationSequencerWindow()
    : EditorWindow("Sequencer")
{
    m_sequencerImpl = std::make_unique<AnimationSequenceImpl>();

    // 編集バッファを ImSequencer へつなぐ
    m_sequencerImpl->SetSequence(&m_editingSequence);
    m_sequencerImpl->SyncFrameCache();

    SetVisible(true);
}



AnimationSequencerWindow::~AnimationSequencerWindow() = default;

void AnimationSequencerWindow::DrawContents(EditorContext &context)
{
    // 処理フロー:
    // 1. 固定中のポーズの一覧を出す
    // 2. 対象のエンティティとモデルを確かめる
    // 3. エンティティの切り替えを処理し、ファイルの読み書きを描く
    // 4. 編集するアニメーションを選ぶ
    // 5. 編集モードを適用し、タイムラインとインスペクタを描く

    // ---------------------------------------------------------
    // 1. 固定中のポーズ
    // ---------------------------------------------------------

    // 何も選んでいなくても解除できるように先頭に出す
    if (context.world)
    {
        DrawPinnedPoseList(context);
    }

    // ---------------------------------------------------------
    // 2. 対象の確認
    // ---------------------------------------------------------
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

    // ---------------------------------------------------------
    // 3. エンティティの切り替えとファイル
    // ---------------------------------------------------------
    ResetStateOnEntitySwitch(context);

    DrawFileIO();

    // ---------------------------------------------------------
    // 4. アニメーションの選択
    // ---------------------------------------------------------
    char nameBuf[128];
    strcpy_s(nameBuf, sizeof(nameBuf), m_editingSequence.sequenceName.c_str());
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputText("Sequence Name", nameBuf, sizeof(nameBuf)))
        m_editingSequence.sequenceName = nameBuf;

    ImGui::SameLine();

    // ロード直後の同期（JSON の targetAnimName から、コンボボックスの選択位置を復元する）
    if (m_prevSelectedAnimIndex == -2)
    {
        m_selectedAnimIndex = 0; // 見つからなかった場合のデフォルト
        for (int i = 0; i < (int)animations.size(); ++i)
        {
            if (animations[i].name == m_editingSequence.targetAnimName)
            {
                m_selectedAnimIndex = i;
                break;
            }
        }
        m_prevSelectedAnimIndex = m_selectedAnimIndex;

        // 選んだアニメーション名をシーケンスへ書き戻す
        //   新規や、モデルにない名前だった場合は上で 0 番へフォールバックしているので、
        //   書き戻さないと targetAnimName が空のまま残る

        // 空のままだと UpdateManual が GetAnimationIndex で -1 を引き、再生しても姿勢が変わらない
        //   コンボボックスは animations[] 側の名前を出すので、見た目では気付けない
        m_editingSequence.targetAnimName = animations[m_selectedAnimIndex].name;

        // 長さはアセットに合わせる
        m_editingSequence.duration = (std::max)(0.01f, animations[m_selectedAnimIndex].secondsLength);
        m_sequencerImpl->SyncFrameCache();
        m_fitCurveView = true;
    }

    ImGui::SetNextItemWidth(250);
    if (ImGui::BeginCombo("Target Animation", animations[m_selectedAnimIndex].name.c_str()))
    {
        for (int i = 0; i < (int)animations.size(); ++i)
        {
            bool sel = (m_selectedAnimIndex == i);
            if (ImGui::Selectable(animations[i].name.c_str(), sel))
                m_selectedAnimIndex = i;
            if (sel)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    // コンボボックスで選択が変更された時の同期
    if (m_prevSelectedAnimIndex != m_selectedAnimIndex)
    {
        m_prevSelectedAnimIndex          = m_selectedAnimIndex;
        m_editingSequence.targetAnimName = animations[m_selectedAnimIndex].name;

        // 長さはアセットに合わせる
        m_editingSequence.duration = (std::max)(0.01f, animations[m_selectedAnimIndex].secondsLength);

        m_sequencerImpl->SyncFrameCache();
        m_fitCurveView = true;
    }

    // ---------------------------------------------------------
    // 5. 編集モードとメインの表示
    // ---------------------------------------------------------
    DrawEditModeToggle(context, animator);

    ImGui::Separator();

    // 左にタイムライン、右にインスペクタとカーブを並べる
    if (!ImGui::BeginTable("SeqLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
        return;

    ImGui::TableSetupColumn("Timeline", ImGuiTableColumnFlags_WidthStretch, 0.7f);
    ImGui::TableSetupColumn("Inspector", ImGuiTableColumnFlags_WidthStretch, 0.3f);
    ImGui::TableNextRow();

    // --- 左: タイムライン ---
    ImGui::TableSetColumnIndex(0);
    const int prevSelectedEntry = m_selectedEntry;
    DrawTimelinePane(context, animator, model);

    // タイムラインでイベントを選び直したら、カーブのタブを開いていてもイベントのタブへ戻す
    const bool focusEventTab = (m_selectedEntry != prevSelectedEntry && m_selectedEntry >= 1);

    // --- 右: インスペクタとカーブ ---
    // イベントの調整とカーブの調整は同時にしないので、タブで切り替えて片方だけを広く出す
    ImGui::TableSetColumnIndex(1);
    if (ImGui::BeginTabBar("##SeqSideTabs"))
    {
        if (ImGui::BeginTabItem("イベント", nullptr, focusEventTab ? ImGuiTabItemFlags_SetSelected : 0))
        {
            DrawInspectorPane(context);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("カーブ"))
        {
            DrawCurvePane(context.animEditTime);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::EndTable();
}



void AnimationSequencerWindow::ResetStateOnEntitySwitch(EditorContext &context)
{
    if (context.selectedEntity != m_lastSelectedEntity)
    {
        // 解除対象は「切替前」のエンティティ（m_lastSelectedEntity はまだ古い方を指している）
        //   選択中の Animator へ復元すると、旧エンティティの override が残ったまま、
        //   新エンティティに別モデルのシーケンスが差し込まれる
        if (m_editingEntity != CCL::ECS::InvalidEntityID)
            ReleaseEditMode(context, m_editingEntity);

        m_editingSequence = AnimSequence{};
        m_savePath.clear();
        m_selectedAnimIndex     = -1;
        m_prevSelectedAnimIndex = -2;
        m_isPlaying             = false;
        m_wasEditMode           = false;
        m_savedSequence         = nullptr;
        m_selectedEntry         = -1;
        m_firstFrame            = 0;
        m_fitCurveView          = true;

        m_sequencerImpl->SetSequence(&m_editingSequence);
        m_sequencerImpl->SyncFrameCache();
        m_lastSelectedEntity = context.selectedEntity;
    }
}



// ---------------------------------------------------------------------------
// ファイルの読み書き
// ---------------------------------------------------------------------------

void AnimationSequencerWindow::DrawFileIO()
{
    // Duration はアセットから取るので、表示だけにする（編集できない）
    ImGui::Text("Editing: %s", m_savePath.empty() ? "(New Sequence)" : m_savePath.c_str());
    ImGui::SameLine();

    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Duration: %.2f s (%d frames)",
                       m_editingSequence.duration, SecToFrame(m_editingSequence.duration));
    ImGui::SameLine();

    if (ImGui::Button("Save JSON..."))
    {
        if (auto path = FileDialog::SavePreset("AnimSequence", "Save Anim Sequence"))
        {
            m_savePath                     = FileDialog::ConvertToRelativePath(*path);
            m_editingSequence.sequenceName = std::filesystem::path(m_savePath).stem().string();

            m_sequencerImpl->SyncFrameCache();

            nlohmann::json j = m_editingSequence;
            if (FileSystem::WriteAllText(m_savePath, j.dump(4)))
            {
                CCL_LOG_SUCCESS(LogCategory::Editor, "Sequence Saved: %s", m_savePath.c_str());

                // 保存した直後に、ResourceManager が持つ同じインスタンスへ読み直させる
                //   読み直さないと起動時の古い内容を返し続け、ゲームの再生に編集が反映されない
                ResourceManager::Instance().ReloadAnimSequence(m_savePath.c_str());
            }
            else
                CCL_LOG_ERROR(LogCategory::Editor, "Sequence Save Failed: %s", m_savePath.c_str());
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
                    nlohmann::json j  = nlohmann::json::parse(text);
                    m_editingSequence = j.get<AnimSequence>();
                    m_savePath        = loadPath;

                    // 次の描画でアニメーションの一覧と同期させる
                    m_prevSelectedAnimIndex = -2;

                    m_sequencerImpl->SetSequence(&m_editingSequence);
                    m_sequencerImpl->SyncFrameCache();

                    m_selectedEntry = -1;
                    m_fitCurveView  = true;
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



// ---------------------------------------------------------------------------
// 編集モード
// ---------------------------------------------------------------------------

void AnimationSequencerWindow::DrawEditModeToggle(EditorContext &context, AnimatorComponent *animator)
{
    if (context.isAnimEditMode)
    {
        if (!m_wasEditMode)
        {
            // 固定中のエンティティを編集し直すときは、先に固定を解いて固定前へ戻す
            //   戻さないと、固定側の写しを「編集前のシーケンス」として控えてしまい、解除しても元へ戻らない
            const int pinIndex = FindPinnedPose(context.selectedEntity);
            if (pinIndex >= 0)
            {
                UnpinPose(context, static_cast<size_t>(pinIndex));
            }

            m_savedSequence      = animator->currentSequence;
            m_wasEditMode        = true;
            context.animEditTime = animator->currentTimer;
        }
        animator->isEditorOverride = true;
        animator->currentSequence  = &m_editingSequence;

        // 「今フレームも編集モードを維持した」印
        //   これが途切れたフレームを OnFrameBegin が検出して解除する（ウィンドウを閉じた・畳んだ場合）
        m_editingEntity     = context.selectedEntity;
        m_editModeRefreshed = true;
    }
    else if (m_wasEditMode)
    {
        ReleaseEditMode(context, context.selectedEntity);
    }
}



void AnimationSequencerWindow::ReleaseEditMode(EditorContext &context, CCL::ECS::EntityID target)
{
    // 編集モードを抜ける経路はすべてここを通る

    if (context.world && target != CCL::ECS::InvalidEntityID)
    {
        if (auto *animator = context.world->GetComponent<AnimatorComponent>(target))
        {
            if (m_savedSequence)
                animator->currentSequence = m_savedSequence;
            animator->isEditorOverride = false;
        }

        // ルートモーションプレビューで動かした位置を元に戻す
        if (m_rootMotionActive)
        {
            if (auto *trans = context.world->GetComponent<TransformComponent>(target))
                trans->SetPosition(m_rootMotionOriginPos);
        }
    }

    ResetEditModeState(context);
}



void AnimationSequencerWindow::ResetEditModeState(EditorContext &context)
{
    m_rootMotionActive     = false;
    m_savedSequence        = nullptr;
    m_wasEditMode          = false;
    m_isPlaying            = false;
    m_editingEntity        = CCL::ECS::InvalidEntityID;
    m_editModeRefreshed    = false;
    context.isAnimEditMode = false;
}



void AnimationSequencerWindow::OnFrameBegin(EditorContext &context)
{
    // 処理フロー:
    // 1. 固定のうち、もう無効になったものを捨てる
    // 2. 描画されなかったフレーム（閉じた・畳んだ・選択を外した）を検出して編集モードを解く

    // ---------------------------------------------------------
    // 1. 固定の掃除（ウィンドウを閉じていても毎フレーム行う）
    // ---------------------------------------------------------
    PrunePinnedPoses(context);

    // ---------------------------------------------------------
    // 2. 編集モードの解除
    // ---------------------------------------------------------
    if (m_editingEntity == CCL::ECS::InvalidEntityID)
        return;

    // 直前のフレームで DrawContents が維持していれば、まだ編集は生きている
    const bool maintained = m_editModeRefreshed;
    m_editModeRefreshed   = false;
    if (maintained)
        return;

    // 維持されなかった＝もう画面に出ていない
    //   ここで解除しないと isEditorOverride が残り、アニメ・遷移・ルートモーションが止まったままになる
    ReleaseEditMode(context, m_editingEntity);
}



// ---------------------------------------------------------------------------
// ポーズの固定
// ---------------------------------------------------------------------------

void AnimationSequencerWindow::PinCurrentPose(EditorContext &context, AnimatorComponent *animator)
{
    // 処理フロー:
    // 1. シーケンスの写しと、解除時の戻し先を固定側へ移す
    // 2. Animator は止めたまま、指すシーケンスだけ写しへ差し替える
    // 3. 編集モードだけを抜ける（エンティティは戻さない）

    // ---------------------------------------------------------
    // 1. 固定側へ移す
    // ---------------------------------------------------------
    PinnedPose pin;
    pin.entity        = context.selectedEntity;
    pin.sequence      = std::make_unique<AnimSequence>(m_editingSequence);
    pin.savedSequence = m_savedSequence;
    pin.time          = context.animEditTime;
    pin.originPos     = m_rootMotionOriginPos;
    pin.restorePos    = m_rootMotionActive;

    // ---------------------------------------------------------
    // 2. Animator を止めたままにする
    // ---------------------------------------------------------
    // ポーズは今フレームの UpdateManual で Model へ当て済み
    //   isEditorOverride の間は AnimationPoseSystem が上書きしないので、そのまま残る
    //   Model はエンティティごとの実体なので、他のキャラにも上書きされない
    animator->currentSequence  = pin.sequence.get();
    animator->currentTimer     = pin.time;
    animator->isEditorOverride = true;

    m_pinnedPoses.push_back(std::move(pin));

    // ---------------------------------------------------------
    // 3. 編集モードを抜ける
    // ---------------------------------------------------------
    // ReleaseEditMode は通さない。通すとシーケンスと位置を戻してしまう
    ResetEditModeState(context);
}



void AnimationSequencerWindow::UnpinPose(EditorContext &context, size_t index)
{
    if (index >= m_pinnedPoses.size()) return;

    PinnedPose &pin = m_pinnedPoses[index];

    if (context.world)
    {
        if (auto *animator = context.world->GetComponent<AnimatorComponent>(pin.entity))
        {
            if (pin.savedSequence)
            {
                animator->currentSequence = pin.savedSequence;
            }
            animator->isEditorOverride = false;
        }

        // ルートモーションのプレビューで動かした位置を元に戻す
        if (pin.restorePos)
        {
            if (auto *trans = context.world->GetComponent<TransformComponent>(pin.entity))
            {
                trans->SetPosition(pin.originPos);
            }
        }
    }

    m_pinnedPoses.erase(m_pinnedPoses.begin() + static_cast<std::ptrdiff_t>(index));
}



void AnimationSequencerWindow::UnpinAllPoses(EditorContext &context)
{
    // 後ろから解く（erase で前の添字がずれないように）
    while (!m_pinnedPoses.empty())
    {
        UnpinPose(context, m_pinnedPoses.size() - 1);
    }
}



int AnimationSequencerWindow::FindPinnedPose(CCL::ECS::EntityID entity) const
{
    for (size_t i = 0; i < m_pinnedPoses.size(); ++i)
    {
        if (m_pinnedPoses[i].entity == entity)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}



void AnimationSequencerWindow::PrunePinnedPoses(EditorContext &context)
{
    if (m_pinnedPoses.empty()) return;

    // World がない（シーンの切り替え中など）なら、固定していたエンティティはもうない
    if (!context.world)
    {
        m_pinnedPoses.clear();
        return;
    }

    // 無効になった固定は、エンティティに触らずに捨てる（別物に戻し処理を掛けないため）
    std::erase_if(m_pinnedPoses, [&](const PinnedPose &pin) {
        const auto *animator = context.world->GetComponent<AnimatorComponent>(pin.entity);
        return !animator || animator->currentSequence != pin.sequence.get();
    });
}



void AnimationSequencerWindow::DrawPinnedPoseList(EditorContext &context)
{
    if (m_pinnedPoses.empty()) return;

    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "固定中のポーズ (%d)", static_cast<int>(m_pinnedPoses.size()));
    ImGui::SameLine();
    if (ImGui::SmallButton("すべて解除"))
    {
        UnpinAllPoses(context);
        return;
    }

    // 解除は 1 フレームに 1 つだけ（ループ中に erase すると添字がずれる）
    int unpinIndex = -1;
    for (size_t i = 0; i < m_pinnedPoses.size(); ++i)
    {
        const PinnedPose &pin = m_pinnedPoses[i];

        const auto *nameComp = context.world->GetComponent<NameComponent>(pin.entity);
        const char *name     = nameComp ? nameComp->name : "(no name)";

        ImGui::PushID(static_cast<int>(i));
        ImGui::BulletText("%s  [%s @ %.2f s]", name, pin.sequence->targetAnimName.c_str(), pin.time);
        ImGui::SameLine();
        if (ImGui::SmallButton("解除"))
        {
            unpinIndex = static_cast<int>(i);
        }
        ImGui::PopID();
    }

    if (unpinIndex >= 0)
    {
        UnpinPose(context, static_cast<size_t>(unpinIndex));
    }

    ImGui::Separator();
}



// ---------------------------------------------------------------------------
// ルートモーションのプレビュー
// ---------------------------------------------------------------------------

void AnimationSequencerWindow::DrawRootMotionPreview(EditorContext &context, AnimatorComponent * /*animator*/)
{
    if (!context.isAnimEditMode || !context.world) return;

    TransformComponent *trans = context.world->GetComponent<TransformComponent>(context.selectedEntity);
    if (!trans) return;

    const CurveData &rmc = m_editingSequence.rootMotionCurve;
    if (rmc.empty())
    {
        // カーブがない（全キーを消した）ならプレビューを止める
        //   次にキーが追加された瞬間の位置を、新しい基準点にする
        m_rootMotionActive = false;
        return;
    }

    const float dur      = m_editingSequence.duration;
    const float progress = (dur > 0.0f) ? std::clamp(context.animEditTime / dur, 0.0f, 1.0f) : 0.0f;

    if (!m_rootMotionActive)
    {
        // 初めてプレビューが有効になったフレームで、今の位置を基準点として控える
        //   以後は毎フレーム「基準点 + 前方 * (curve(progress) - curve(0))」で絶対位置を求め直す

        // 基準点（m_rootMotionBasePos）には、ループのたびに 1 周ぶんの前進を足す（DrawTimelinePane）
        //   m_rootMotionOriginPos は足さない本当の開始位置で、編集モード終了時の復元に使う
        m_rootMotionOriginPos = trans->position;
        m_rootMotionBasePos   = trans->position;
        m_rootMotionActive    = true;
    }

    // ワールドの前方（worldMatrix の Z 軸。キャッシュ済みの回転を使う）
    DirectX::XMFLOAT3 fwd = trans->GetForward();

    const float dist = rmc.Evaluate(progress) - rmc.Evaluate(0.0f);

    DirectX::XMFLOAT3 newPos = {
        m_rootMotionBasePos.x + fwd.x * dist,
        m_rootMotionBasePos.y + fwd.y * dist,
        m_rootMotionBasePos.z + fwd.z * dist,
    };
    trans->SetPosition(newPos);

    // worldMatrix は本来 TransformUpdateSystem が求めるが、この後すぐ model->UpdateTransform() が読むので、ここで求め直す
    //   親を持つエンティティでは親の行列を見ていないので位置がずれる（プレビュー専用の簡易実装）
    trans->UpdateMatrix();
}



// ---------------------------------------------------------------------------
// タイムライン
// ---------------------------------------------------------------------------

void AnimationSequencerWindow::DrawTimelinePane(EditorContext &context, AnimatorComponent *animator, Model *model)
{
    // 処理フロー:
    // 1. プレビュー再生の時間を進める
    // 2. 再生コントロールと時刻を描く
    // 3. スナップの設定とイベントの追加ボタンを描く
    // 4. タイムラインを描き、ドラッグ・削除・ナッジを反映する
    // 5. スクラブを反映する
    // 6. プレビュー（移動量・ポーズ・判定球）を反映する

    // ---------------------------------------------------------
    // 1. プレビュー再生
    // ---------------------------------------------------------
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Space))
        m_isPlaying = !m_isPlaying;

    const float dur      = m_editingSequence.duration;
    const int   durFrame = SecToFrame(dur);

    if (context.isAnimEditMode)
    {
        if (m_isPlaying)
        {
            const float progress0 = (dur > 0.0f) ? std::clamp(context.animEditTime / dur, 0.0f, 1.0f) : 0.0f;
            const float speedMult = m_editingSequence.speedCurve.empty()
                                        ? 1.0f
                                        : m_editingSequence.speedCurve.Evaluate(progress0);
            context.animEditTime += ImGui::GetIO().DeltaTime * animator->playbackSpeed * speedMult;
            if (dur > 0.0f && context.animEditTime > dur)
            {
                // ループはプレビュー専用のトグルで決める（アニメの設定と別に試せる）
                if (m_loopPreview)
                {
                    context.animEditTime = std::fmod(context.animEditTime, dur);

                    // 1 周ぶんの前進量を、ルートモーションのプレビューの基準位置へ足す
                    //   次のループの進捗 0 が前のループの終端と一致し、原点へ戻らずに前進し続ける
                    //   ここは再生による折り返しでだけ通る（スクラブでは通らない）
                    if (m_rootMotionActive && !m_editingSequence.rootMotionCurve.empty() && context.world)
                    {
                        if (auto *trans = context.world->GetComponent<TransformComponent>(context.selectedEntity))
                        {
                            const CurveData  &rmc       = m_editingSequence.rootMotionCurve;
                            const float       cycleDist = rmc.Evaluate(1.0f) - rmc.Evaluate(0.0f);
                            DirectX::XMFLOAT3 fwd       = trans->GetForward();
                            m_rootMotionBasePos.x += fwd.x * cycleDist;
                            m_rootMotionBasePos.y += fwd.y * cycleDist;
                            m_rootMotionBasePos.z += fwd.z * cycleDist;
                        }
                    }
                }
                else
                {
                    context.animEditTime = dur;
                    m_isPlaying          = false; // 非ループは最後で停止
                }
            }
            animator->currentTimer = context.animEditTime;
        }

        // ---------------------------------------------------------
        // 2. 再生コントロール
        // ---------------------------------------------------------
        if (ImGui::Button("|<")) // 先頭へ
        {
            context.animEditTime   = 0.0f;
            animator->currentTimer = 0.0f;
            m_isPlaying            = false;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("先頭へ (Home)");
        ImGui::SameLine();

        if (ImGui::Button(m_isPlaying ? "|| Pause" : "> Play"))
            m_isPlaying = !m_isPlaying;
        ImGui::SameLine();

        if (ImGui::Button(">|")) // 最後へ
        {
            context.animEditTime   = dur;
            animator->currentTimer = dur;
            m_isPlaying            = false;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("最後へ (End)");
        ImGui::SameLine();

        ImGui::Checkbox("Loop", &m_loopPreview);
        ImGui::SameLine();

        // --- 今の時刻（秒 / フレーム / 全体の長さ）---
        const float curSec = context.animEditTime;
        const int   curFrm = SecToFrame(curSec);
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1.0f),
                           "%.2fs / %df", curSec, curFrm);
        ImGui::SameLine();
        ImGui::TextDisabled("(total %.2fs / %df)", dur, durFrame);
    }
    else
    {
        m_isPlaying = false;
        ImGui::TextDisabled("Enable 'Edit Mode' to preview & scrub.");
    }

    // --- Home / End キーでも先頭・末尾へ ---
    if (context.isAnimEditMode && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Home))
        {
            context.animEditTime   = 0.0f;
            animator->currentTimer = 0.0f;
            m_isPlaying            = false;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_End))
        {
            context.animEditTime   = dur;
            animator->currentTimer = dur;
            m_isPlaying            = false;
        }
    }

    // ---------------------------------------------------------
    // 3. スナップとイベントの追加
    // ---------------------------------------------------------
    ImGui::Checkbox("Snap", &m_snapEnabled);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::DragInt("##snapStep", &m_snapStep, 1, 1, 30, "%df");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("スナップ単位（フレーム）");
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // 今の再生位置から 30 フレームの区間でイベントを足し、選択する
    auto AddEvt = [&](AnimEventType t) {
        m_sequencerImpl->Add((int)t);
        auto &e     = m_editingSequence.events.back();
        int   s     = context.isAnimEditMode ? SecToFrame(context.animEditTime) : 0;
        e.startTime = FrameToSec(s);
        e.endTime   = FrameToSec(s + 30);
        m_sequencerImpl->SyncFrameCache();
        m_selectedEntry = (int)m_editingSequence.events.size();
    };

    if (ImGui::Button("+ Attack"))
        AddEvt(AnimEventType::HitBox);
    ImGui::SameLine();
    if (ImGui::Button("+ Spawn"))
        AddEvt(AnimEventType::SpawnHitbox);
    ImGui::SameLine();
    if (ImGui::Button("+ Trail"))
        AddEvt(AnimEventType::Trail);
    ImGui::SameLine();
    if (ImGui::Button("+ Particle"))
        AddEvt(AnimEventType::Particle);
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

    // ---------------------------------------------------------
    // 4. タイムライン
    // ---------------------------------------------------------
    int curFrame  = context.isAnimEditMode ? SecToFrame(context.animEditTime) : SecToFrame(animator->currentTimer);
    int prevFrame = curFrame;

    m_expanded   = true;
    bool changed = ImSequencer::Sequencer(
        m_sequencerImpl.get(), &curFrame, &m_expanded, &m_selectedEntry, &m_firstFrame,
        ImSequencer::SEQUENCER_EDIT_STARTEND | ImSequencer::SEQUENCER_ADD |
            ImSequencer::SEQUENCER_DEL | ImSequencer::SEQUENCER_CHANGE_FRAME);

    // --- ドラッグ後のスナップ（選んだイベントの境界をスナップ単位へ丸める）---
    if (changed)
    {
        if (m_snapEnabled && m_snapStep > 0 &&
            m_selectedEntry > 0 && m_selectedEntry < (int)m_sequencerImpl->m_frameCacheMap.size())
        {
            auto &f    = m_sequencerImpl->m_frameCacheMap[m_selectedEntry];
            auto  snap = [&](int v) { return ((v + m_snapStep / 2) / m_snapStep) * m_snapStep; };
            f.first    = snap(f.first);
            f.second   = snap(f.second);
            if (f.second <= f.first)
                f.second = f.first + m_snapStep; // 区間が潰れない最小幅
        }
        m_sequencerImpl->FlushFrameCache();
    }

    // --- Delete キーで削除 ---
    if (m_selectedEntry > 0 && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Delete))
    {
        m_sequencerImpl->Del(m_selectedEntry);
        m_selectedEntry = -1;
    }

    // --- 矢印キーで 1 フレーム（Shift で 5 フレーム）ずらす ---
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            m_selectedEntry = -1;

        int nudge = ImGui::GetIO().KeyShift ? 5 : 1;
        if (m_selectedEntry > 0 && m_selectedEntry < (int)m_sequencerImpl->m_frameCacheMap.size()) // マスターはナッジ不可
        {
            auto &f     = m_sequencerImpl->m_frameCacheMap[m_selectedEntry];
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
                m_sequencerImpl->FlushFrameCache();
        }
    }

    // ---------------------------------------------------------
    // 5. スクラブ
    // ---------------------------------------------------------

    // 左ボタンを押している（実際にドラッグしている）ときだけスクラブとみなす
    //   curFrame != prevFrame は、何も触っていなくても ImSequencer 内部の丸めだけで起こりうる

    // 丸めをスクラブとみなすと、再生中に不規則な停止と巻き戻り・早送りが起き、速度が変わって見える
    //   再生中の時間の進行は、1 のブロックだけが決める
    if (curFrame != prevFrame && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        m_isPlaying            = false;
        context.animEditTime   = FrameToSec(curFrame);
        animator->currentTimer = context.animEditTime;
    }

    // ---------------------------------------------------------
    // 6. プレビューの反映
    // ---------------------------------------------------------

    // ルートモーションの前進量を Transform に反映する
    //   下の UpdateTransform は worldMatrix を読むだけなので、先に位置を確定させる
    DrawRootMotionPreview(context, animator);

    // 今の時刻のポーズを Model へ当てる
    if (context.isAnimEditMode && context.systemManager)
    {
        if (auto *animSys = context.systemManager->GetSystem<AnimationSystem>())
        {
            animSys->UpdateManual(context.selectedEntity, context.animEditTime);
            if (auto *tr = context.world->GetComponent<TransformComponent>(context.selectedEntity))
                model->UpdateTransform(tr->worldMatrix);
        }
    }

    // ルートモーションで位置を確定させた後に描く（描く位置がキャラとずれないように）
    DrawHitboxPreview(context);
}



// ---------------------------------------------------------------------------
// 攻撃判定のプレビュー
// ---------------------------------------------------------------------------

void AnimationSequencerWindow::DrawHitboxPreview(EditorContext &context)
{
    if (!context.isAnimEditMode || !context.world) return;
    if (!context.world->HasResource<ShapeRenderer *>()) return;
    ShapeRenderer *shapeRenderer = context.world->GetResource<ShapeRenderer *>();
    if (!shapeRenderer) return;

    const auto *trans = context.world->GetComponent<TransformComponent>(context.selectedEntity);
    if (!trans) return;

    // SpawnHitboxSystem と同じ計算にする。offset はメートルで、キャラの回転だけを掛ける（scale は掛けない）
    const DirectX::XMFLOAT3 ownerPos = trans->GetWorldPosition();
    const DirectX::XMFLOAT4 ownerRot = trans->GetWorldRotation();
    const DirectX::XMVECTOR rotQ     = DirectX::XMLoadFloat4(&ownerRot);

    const float now = context.animEditTime;
    for (size_t i = 0; i < m_editingSequence.events.size(); ++i)
    {
        const auto &ev = m_editingSequence.events[i];
        if (ev.type != AnimEventType::SpawnHitbox) continue;

        // イベントのインデックスは m_selectedEntry - 1（0 はマスタートラック）
        const bool selected = (m_selectedEntry - 1 == static_cast<int>(i));
        const bool active   = (ev.startTime <= now && now <= ev.endTime);
        if (!selected && !active) continue;

        const DirectX::XMVECTOR off = DirectX::XMVectorSet(ev.offset.x, ev.offset.y, ev.offset.z, 0.0f);
        DirectX::XMFLOAT3       center;
        DirectX::XMStoreFloat3(&center,
                               DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&ownerPos), DirectX::XMVector3Rotate(off, rotQ)));

        // 区間に入っている（＝ゲームなら判定が出ている）ときは赤、選んでいるだけのときは薄い黄色
        const DirectX::XMFLOAT4 color = active ? DirectX::XMFLOAT4(1.0f, 0.2f, 0.2f, 1.0f)
                                               : DirectX::XMFLOAT4(1.0f, 0.9f, 0.3f, 0.5f);
        shapeRenderer->DrawSphere(center, ev.radius, color);
    }
}



// ---------------------------------------------------------------------------
// インスペクタ（イベントの設定）
// ---------------------------------------------------------------------------

void AnimationSequencerWindow::DrawInspectorPane(EditorContext &context)
{
    // 処理フロー:
    // 1. マスター（または未選択）なら、長さと編集モードを出す
    // 2. イベントなら、開始・終了の時刻を出す
    // 3. 種別ごとの設定を出す

    ImGui::Text("Sequence Settings");
    ImGui::Separator();

    // ---------------------------------------------------------
    // 1. マスター（または未選択）
    // ---------------------------------------------------------
    if (m_selectedEntry == 0 || m_selectedEntry == -1)
    {
        if (m_selectedEntry == 0)
        {
            ImGui::TextColored(ImVec4(0.5f, 1, 0.5f, 1), "Master Track (Duration)");
            ImGui::TextDisabled("長さはアニメーションのアセットで決まる（ここでは変更できない）。");
        }

        // 長さはアセットで決まるので表示だけ
        ImGui::Text("Duration: %.2f s", m_editingSequence.duration);

        // 再生の開始位置は、シーケンスごとに決める
        ImGui::SetNextItemWidth(90);
        if (ImGui::DragFloat("再生開始 (s)##startOffset", &m_editingSequence.startOffset, 0.01f, 0.0f,
                             m_editingSequence.duration, "%.2f"))
        {
            m_editingSequence.startOffset = std::clamp(m_editingSequence.startOffset, 0.0f, m_editingSequence.duration);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("ステートに入ったとき、この時刻から再生します。\n"
                              "例: ジャンプの溜めを飛ばし、踏み切りの瞬間から見せる。0 で頭から。");
        }

        ImGui::Checkbox("Edit Mode (Preview)", &context.isAnimEditMode);

        // 編集モード中だけ、今のポーズで止めておける
        if (context.isAnimEditMode && context.world)
        {
            if (auto *animator = context.world->GetComponent<AnimatorComponent>(context.selectedEntity))
            {
                if (ImGui::Button("このポーズで固定"))
                {
                    PinCurrentPose(context, animator);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("今の再生位置のまま止めて、編集モードを抜けます。\n"
                                      "選択を変えても止まったままなので、複数体を止めてからカメラを動かせます。");
                }
            }
        }
    }
    else if (m_selectedEntry > 0 && m_selectedEntry <= (int)m_editingSequence.events.size())
    {
        // ---------------------------------------------------------
        // 2. イベントの時刻
        // ---------------------------------------------------------
        auto &e  = m_editingSequence.events[m_selectedEntry - 1]; // 0 はマスタートラックなので 1 ずらす
        bool  ch = false;

        ImGui::TextColored(ImVec4(0.5f, 1, 0.5f, 1), "Selected Event");
        ImGui::Spacing();
        ImGui::TextDisabled("Type: %s", m_sequencerImpl->GetItemTypeName((int)e.type));
        // 開始・終了を秒で入力する（タイムラインのドラッグより細かく合わせられる）
        {
            float startSec = e.startTime;
            float endSec   = e.endTime;
            bool  timeCh   = false;

            ImGui::SetNextItemWidth(90);
            if (ImGui::DragFloat("開始 (s)##start", &startSec, 0.01f, 0.0f, m_editingSequence.duration, "%.2f"))
                timeCh = true;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90);
            if (ImGui::DragFloat("終了 (s)##end", &endSec, 0.01f, 0.0f, m_editingSequence.duration, "%.2f"))
                timeCh = true;

            // フレーム表示も併記
            ImGui::TextDisabled("Frame: %d - %d", SecToFrame(e.startTime), SecToFrame(e.endTime));

            if (timeCh)
            {
                // クランプと前後関係の保証
                startSec = std::clamp(startSec, 0.0f, m_editingSequence.duration);
                endSec   = std::clamp(endSec, 0.0f, m_editingSequence.duration);
                if (endSec < startSec)
                    endSec = startSec;

                e.startTime = startSec;
                e.endTime   = endSec;
                ch          = true;
                m_sequencerImpl->SyncFrameCache(); // タイムライン側へ即反映
            }
        }
        ImGui::Spacing();
        ImGui::Spacing();

        char buf[256];
        strcpy_s(buf, sizeof(buf), e.stringParam.c_str());

        // ---------------------------------------------------------
        // 3. 種別ごとの設定
        // ---------------------------------------------------------

        // 武器タグの選択（CombatRoster があれば一覧から、なければ手入力）。HitBox / Trail / Particle で使う
        auto drawWeaponTagPicker = [&](AnimNotifyEvent &ev, bool &changed) {
            auto *roster = context.world->GetComponent<CombatRosterComponent>(context.selectedEntity);
            if (roster && roster->count > 0)
            {
                ImGui::Text("Weapon Tag:");
                ImGui::SetNextItemWidth(-1);
                const char *preview = ev.stringParam.empty() ? "Select Tag..." : ev.stringParam.c_str();
                if (ImGui::BeginCombo("##Tag", preview))
                {
                    for (int i = 0; i < roster->count; ++i)
                    {
                        bool sel = (ev.stringParam == roster->entries[i].tag);
                        if (ImGui::Selectable(roster->entries[i].tag, sel))
                        {
                            ev.stringParam = roster->entries[i].tag;
                            changed        = true;
                        }
                        if (sel)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }
            else
            {
                char tagBuf[256];
                strcpy_s(tagBuf, sizeof(tagBuf), ev.stringParam.c_str());
                ImGui::Text("Weapon Tag (no roster):");
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputText("##Tag", tagBuf, sizeof(tagBuf)))
                {
                    ev.stringParam = tagBuf;
                    changed        = true;
                }
            }
        };

        switch (e.type)
        {
        case AnimEventType::HitBox: {
            drawWeaponTagPicker(e, ch);
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1, 0.5f, 0.5f, 1), "Attack");
            if (ImGui::DragFloat("Damage", &e.damage, 0.5f, 0.0f, 1000.0f, "%.1f"))
                ch = true;
            if (ImGui::DragFloat("Knockback", &e.knockback, 0.1f, 0.0f, 100.0f, "%.1f"))
                ch = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("吹き飛ばしの強さ。対ボスは弱め(0〜数値小)推奨");
            ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "Hit Stop");
            if (ImGui::SliderFloat("Duration", &e.hitStopDuration, 0.0f, 1.0f, "%.2f s"))
                ch = true;
            if (ImGui::SliderFloat("TimeScale", &e.hitStopScale, 0.0f, 1.0f, "%.2f x"))
                ch = true;
            break;
        }
        case AnimEventType::SpawnHitbox: {
            ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "Spatial Hitbox");
            if (ImGui::DragFloat3("Offset", &e.offset.x, 0.05f))
                ch = true;
            if (ImGui::DragFloat("Radius", &e.radius, 0.05f, 0.0f, 100.0f))
                ch = true;
            if (ImGui::DragFloat("Damage", &e.damage, 0.5f, 0.0f, 1000.0f, "%.1f"))
                ch = true;
            if (ImGui::DragFloat("Knockback", &e.knockback, 0.1f, 0.0f, 100.0f, "%.1f"))
                ch = true;
            if (ImGui::SliderFloat("HitStop", &e.hitStopDuration, 0.0f, 1.0f, "%.2f s"))
                ch = true;
            if (ImGui::Checkbox("持ち主に追従", &e.hitboxFollow))
                ch = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("ON: キャラに付いていき、アニメが切り替わったら消える（突進など）\n"
                                  "OFF: 生成した場所に残る（衝撃波など）\n"
                                  "Offset はどちらもメートル単位（キャラの scale は掛からない）");

            // 攻撃レイヤー（誰に当てるか）。剛体レイヤーと同じ LayerNames から選ぶ
            int atkLayer = e.attackLayer;
            if (atkLayer < 0 || atkLayer >= PhysicsLayers::LayerCount) atkLayer = 3;
            if (ImGui::Combo("Attack Layer", &atkLayer,
                             PhysicsLayers::LayerNames, PhysicsLayers::LayerCount))
            {
                e.attackLayer = static_cast<uint8_t>(atkLayer);
                ch            = true;
            }
            ImGui::TextDisabled("PLAYER_ATTACK=ボスに命中 / BOSS_ATTACK=プレイヤーに命中");
            break;
        }
        case AnimEventType::Trail:
            ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1), "Weapon Trail (軌跡)");
            drawWeaponTagPicker(e, ch);
            ImGui::Spacing();
            ImGui::TextDisabled("この区間中、対象武器の Weapon Trail が emitting=ON になります");
            ImGui::TextDisabled("(武器側で Anim Controlled を ON にしておくこと)。");
            ImGui::TextDisabled("軌跡の色・尾の長さは武器の Weapon Trail コンポーネントで調整します。");
            ImGui::Spacing();
            ImGui::TextDisabled("Active [%.2fs - %.2fs]", e.startTime, e.endTime);
            break;
        case AnimEventType::Particle:
            ImGui::TextColored(ImVec4(0.85f, 0.5f, 0.25f, 1), "GPU Particle (まとい)");
            drawWeaponTagPicker(e, ch);
            ImGui::Spacing();
            ImGui::TextDisabled("この区間中、対象エミッタの GPU Particle が emitting=ON になります。");
            ImGui::TextDisabled("(エミッタ側で Anim Controlled を ON にし、剣ボーンに子付けしておくこと)。");
            ImGui::TextDisabled("粒子の見た目・力場は GPU Particle Emitter コンポーネントで調整します。");
            ImGui::Spacing();
            ImGui::TextDisabled("Active [%.2fs - %.2fs]", e.startTime, e.endTime);
            break;
        case AnimEventType::Sound:
            ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "Sound (効果音)");
            if (DrawSoundPicker(context, e))
                ch = true;
            if (ImGui::SliderFloat("音量", &e.soundVolume, 0.0f, 2.0f, "%.2f x"))
                ch = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("1 で等倍、0 で無音。Ctrl+クリックで数値入力");
            if (ImGui::SliderFloat("ピッチ", &e.soundPitch, -1.0f, 1.0f, "%.2f oct"))
                ch = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("オクターブ単位。0 で等倍、+1 で 1 オクターブ上、-1 で 1 オクターブ下\n"
                                  "スロー演出中は、これにさらにスロー分の低下が足されます");
            if (ImGui::SliderFloat("ピッチの揺らぎ", &e.soundPitchRandom, 0.0f, 0.5f, "±%.2f oct"))
                ch = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("鳴らすたびに、ピッチをこの幅でランダムにずらします。0 でずらしません\n"
                                  "0.07 oct ≒ ±5%%。連続で振る攻撃の音が機械的に聞こえるのを防ぎます");
            ImGui::Spacing();
            ImGui::TextDisabled("開始時刻に、キャラの位置で 3D のワンショットとして鳴らします。");
            ImGui::TextDisabled("区間の長さは使いません。");
            break;
        case AnimEventType::Effect:
            ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "VFX (パーティクル)");
            if (DrawVfxPrefabPicker(e))
                ch = true;

            // 基準（キャラ or 武器）。武器の候補は選択中のキャラの CombatRoster から出す
            {
                auto       *roster  = context.world->GetComponent<CombatRosterComponent>(context.selectedEntity);
                const char *preview = e.effectAttachTag.empty() ? "キャラ" : e.effectAttachTag.c_str();
                if (ImGui::BeginCombo("基準", preview))
                {
                    if (ImGui::Selectable("キャラ", e.effectAttachTag.empty()))
                    {
                        e.effectAttachTag.clear();
                        ch = true;
                    }
                    if (roster && roster->count > 0)
                    {
                        for (int i = 0; i < roster->count; ++i)
                        {
                            const bool sel = (e.effectAttachTag == roster->entries[i].tag);
                            if (ImGui::Selectable(roster->entries[i].tag, sel))
                            {
                                e.effectAttachTag = roster->entries[i].tag;
                                ch                = true;
                            }
                        }
                    }
                    else
                    {
                        ImGui::TextDisabled("CombatRoster が無いので武器は選べません");
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("VFX を出す位置・向きの基準と、追従するときの親\n"
                                      "武器を選ぶと、剣にまとうオーラなどを剣に沿って出せます");
            }

            if (ImGui::DragFloat3("Offset", &e.offset.x, 0.05f))
                ch = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("基準のローカル座標（キャラなら Z+ が前方）。メートル単位で、基準の scale は掛かりません");
            if (ImGui::DragFloat("Scale", &e.effectScale, 0.05f, 0.01f, 100.0f, "%.2f x"))
                ch = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("粒子の大きさ・発生範囲・速度をまとめて拡大縮小します");
            if (ImGui::Checkbox("基準に追従", &e.effectFollow))
                ch = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("ON: 基準の子になり付いていく（まとうオーラなど）\n"
                                  "OFF: 出した場所に残る（衝撃波・砂煙など）。向きは出した瞬間の基準に合わせる");
            ImGui::Spacing();
            ImGui::TextDisabled("開始時刻に 1 回出します。継続放出のエミッタを含む VFX は、");
            ImGui::TextDisabled("区間の長さだけ出し続けてから止めます。");
            break;
        case AnimEventType::ComboWindow:
        case AnimEventType::CancelWindow: {
            const bool isCombo = (e.type == AnimEventType::ComboWindow);
            ImGui::TextColored(isCombo ? ImVec4(0.25f, 0.75f, 1.0f, 1.0f) : ImVec4(0.75f, 0.25f, 1.0f, 1.0f),
                               isCombo ? "コンボ受付窓 (Combo Window)" : "キャンセル窓 (Cancel Window)");
            ImGui::Separator();

            ImGui::Text("許可するキャンセルアクション:");

            // 選んでいるフラグの名前をつなげて、コンボボックスの表示にする
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

            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##CancelMaskCombo", preview.c_str()))
            {
                for (const auto &def : GetCancelFlagDefs())
                {
                    bool active = (e.cancelMask & def.flag) != 0;

                    // 中身はチェックボックスにする（クリックしてもコンボボックスが閉じないので、続けて選べる）
                    if (ImGui::Checkbox(def.name, &active))
                    {
                        if (active)
                            e.cancelMask |= def.flag;
                        else
                            e.cancelMask &= ~def.flag;
                        ch = true;
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
            m_sequencerImpl->SyncFrameCache();
    }
}



bool AnimationSequencerWindow::DrawSoundPicker(EditorContext &context, AnimNotifyEvent &e)
{
    bool changed = false;

    // AudioConfig からロード済みのイベント名一覧（AudioEmitterComponent のインスペクタと同じ出どころ）
    std::vector<std::string> names;
    if (context.world && context.world->HasResource<std::shared_ptr<IAudioAPI>>())
        if (auto &api = context.world->GetResource<std::shared_ptr<IAudioAPI>>())
            names = api->GetEventNames();

    const std::string current = e.stringParam.empty() ? std::string("<未設定>") : e.stringParam;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##SoundCombo", current.c_str()))
    {
        if (names.empty())
            ImGui::TextDisabled("AudioConfig に登録がありません");
        for (const std::string &n : names)
        {
            const bool selected = (n == e.stringParam);
            if (ImGui::Selectable(n.c_str(), selected))
            {
                e.stringParam = n;
                changed       = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Assets/Config/AudioConfigDXTK.json に登録した音から選びます");

    // 一覧にない名前を直接指定する欄
    char buf[256];
    strcpy_s(buf, sizeof(buf), e.stringParam.c_str());
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##SoundName", buf, sizeof(buf)))
    {
        e.stringParam = buf;
        changed       = true;
    }
    return changed;
}



bool AnimationSequencerWindow::DrawVfxPrefabPicker(AnimNotifyEvent &e)
{
    namespace fs = std::filesystem;
    bool changed = false;

    // 表示はファイル名だけにする（パスは長くて読みにくい）
    const std::string current = e.stringParam.empty() ? std::string("<未設定>")
                                                      : fs::path(e.stringParam).filename().string();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##VfxCombo", current.c_str()))
    {
        // 一覧は開いている間だけ作る（閉じている間は毎フレームディレクトリを走査しない）
        std::error_code ec;
        bool            any = false;
        for (const auto &entry : fs::directory_iterator(kVfxPrefabDir, ec))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".json")
                continue;

            // 拡張子を落として保存する（VFX::Play の呼び出し方にそろえる）
            const std::string stem = entry.path().stem().string();
            const std::string path = std::string(kVfxPrefabDir) + "/" + stem;
            const bool        sel  = (path == e.stringParam);
            if (ImGui::Selectable(stem.c_str(), sel))
            {
                e.stringParam = path;
                changed       = true;
            }
            if (sel) ImGui::SetItemDefaultFocus();
            any = true;
        }
        if (!any)
            ImGui::TextDisabled("%s にプレハブがありません", kVfxPrefabDir);
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s の VFX プレハブから選びます\n（Transform・GPU Particle・TimeState を持つ 1 Entity のもの）", kVfxPrefabDir);

    // 一覧の外のプレハブを直接指定する欄
    char buf[256];
    strcpy_s(buf, sizeof(buf), e.stringParam.c_str());
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##VfxPath", buf, sizeof(buf)))
    {
        e.stringParam = buf;
        changed       = true;
    }
    return changed;
}



// ---------------------------------------------------------------------------
// カーブ
// ---------------------------------------------------------------------------

void AnimationSequencerWindow::DrawCurvePane(float animTime)
{
    // 処理フロー:
    // 1. 表示するカーブを選ぶ
    // 2. 編集するカーブを選ぶ
    // 3. ルートモーションなら、形のプリセットを出す
    // 4. カーブエディタを描く
    // 5. 今の再生位置での値を出す

    // ---------------------------------------------------------
    // 1. 表示の切り替え
    // ---------------------------------------------------------
    ImGui::TextColored(ImVec4(1, 0.6f, 0.2f, 1), "アニメーションカーブ (横軸 = 進捗 0〜1)");

    ImGui::Checkbox("速度##cv", &m_showSpeedCurve);
    ImGui::SameLine();
    ImGui::Checkbox("ルートモーション##cv", &m_showRootCurve);
    ImGui::SameLine();
    if (ImGui::Button("全体表示##cv")) m_fitCurveView = true;
    ImGui::SameLine();
    ImGui::TextDisabled("[ダブルクリック=キー追加 / 右orホイール=パン&ズーム / ハンドル=傾きを手で決める]");

    m_editingSequence.speedCurve.visible      = m_showSpeedCurve;
    m_editingSequence.rootMotionCurve.visible = m_showRootCurve;

    const float dur      = m_editingSequence.duration;
    const float progress = (dur > 0.0f) ? std::clamp(animTime / dur, 0.0f, 1.0f) : 0.0f;

    // ---------------------------------------------------------
    // 2. 編集するカーブ
    // ---------------------------------------------------------

    // 変わったときだけ SetActiveCurve する（毎フレーム呼ぶと、キーの選択が消える）
    int active = m_curveWidget.ActiveCurveIndex();
    int prev   = active;
    ImGui::RadioButton("速度を編集", &active, 0);
    ImGui::SameLine();
    ImGui::RadioButton("ルートモーションを編集", &active, 1);
    if (active != prev) m_curveWidget.SetActiveCurve(active);

    std::vector<CurveData *> targets = {&m_editingSequence.speedCurve, &m_editingSequence.rootMotionCurve};

    // ---------------------------------------------------------
    // 3. 形のプリセット（ルートモーションのときだけ）
    // ---------------------------------------------------------

    // 単調に増える形をボタン 1 つで作れるようにする
    //   カーブは「進捗 → 累積前進距離（m）」で、0 から形を作るには接線を触る必要がある

    // 移動距離は、末尾のキーの値を動かして決める
    //   距離専用の入力欄は置かず、他のキーと同じ操作にそろえる
    if (active == 1)
    {
        CurveData &rc = m_editingSequence.rootMotionCurve;

        auto rebuildPreset = [](CurveData &c, int type, float dist) {
            c.keys.clear();
            CurveKeyframe k0;
            k0.time  = 0.0f;
            k0.value = 0.0f;
            CurveKeyframe k1;
            k1.time  = 1.0f;
            k1.value = dist;
            switch (type)
            {
            case 0: // 一定速度（線形）
                k0.interp = CurveInterp::Linear;
                k1.interp = CurveInterp::Linear;
                break;
            case 1: // イーズイン（ゆっくり踏み出して加速）
                k0.interp  = CurveInterp::Cubic;
                k0.tangent = TangentMode::Flat;
                k1.interp  = CurveInterp::Cubic;
                k1.tangent = TangentMode::Auto;
                break;
            case 2: // イーズアウト（勢いよく出て減速）
                k0.interp  = CurveInterp::Cubic;
                k0.tangent = TangentMode::Auto;
                k1.interp  = CurveInterp::Cubic;
                k1.tangent = TangentMode::Flat;
                break;
            case 3: // スムーズ（S字：加速→減速）
                k0.interp  = CurveInterp::Cubic;
                k0.tangent = TangentMode::Flat;
                k1.interp  = CurveInterp::Cubic;
                k1.tangent = TangentMode::Flat;
                break;
            }
            c.keys.push_back(k0);
            c.keys.push_back(k1);
            c.AssignMissingIds();
            c.RecalculateTangents();
        };

        // 作り直すときの距離は、今のカーブの総移動量を引き継ぐ（なければ 1.0）
        const float keepDist = rc.keys.empty() ? 1.0f : (rc.keys.back().value - rc.keys.front().value);

        ImGui::TextDisabled("形状プリセット:");
        ImGui::SameLine();
        if (ImGui::Button("一定"))
        {
            rebuildPreset(rc, 0, keepDist);
            m_curveWidget.SetActiveCurve(1);
        }
        ImGui::SameLine();
        if (ImGui::Button("イーズイン"))
        {
            rebuildPreset(rc, 1, keepDist);
            m_curveWidget.SetActiveCurve(1);
        }
        ImGui::SameLine();
        if (ImGui::Button("イーズアウト"))
        {
            rebuildPreset(rc, 2, keepDist);
            m_curveWidget.SetActiveCurve(1);
        }
        ImGui::SameLine();
        if (ImGui::Button("スムーズ"))
        {
            rebuildPreset(rc, 3, keepDist);
            m_curveWidget.SetActiveCurve(1);
        }

        ImGui::TextDisabled("移動距離は末尾キーの「値」を、キャンバスか下のキー設定でドラッグして決めます。");
    }

    // ---------------------------------------------------------
    // 4. カーブエディタ
    // ---------------------------------------------------------
    // タブで列を占有できるので、残りの高さの半分までキャンバスを広げる（下にキー設定が入る）
    CurveEditorWidget::Options opt;
    opt.height        = (std::max)(240.0f, ImGui::GetContentRegionAvail().y * 0.5f);
    opt.showInspector = true;
    opt.showPlayhead  = true;
    opt.playheadX     = progress;
    opt.fitRequested  = m_fitCurveView;
    m_fitCurveView    = false;

    m_curveWidget.Draw("##seqCurves", targets, opt);

    // ---------------------------------------------------------
    // 5. 今の再生位置での値
    // ---------------------------------------------------------
    const float sp = m_editingSequence.speedCurve.empty() ? 1.0f : m_editingSequence.speedCurve.Evaluate(progress);
    const float rm = m_editingSequence.rootMotionCurve.empty() ? 0.0f : m_editingSequence.rootMotionCurve.Evaluate(progress);

    // ルートモーションの瞬間の速さ（傾き）も出す。前後 0.01 の差分で求める
    float rmSpeed = 0.0f;
    if (!m_editingSequence.rootMotionCurve.empty() && dur > 0.0f)
    {
        const float      h   = 0.01f;
        const float      p0  = std::clamp(progress - h, 0.0f, 1.0f);
        const float      p1  = std::clamp(progress + h, 0.0f, 1.0f);
        const CurveData &rc2 = m_editingSequence.rootMotionCurve;
        const float      dp  = (p1 - p0) * dur; // 秒
        if (dp > 1e-5f) rmSpeed = (rc2.Evaluate(p1) - rc2.Evaluate(p0)) / dp;
    }

    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f),
                       "進捗 %.2f  |  速度倍率 %.3f  |  累積前進 %.3f m  |  前進速度 %.2f m/s", progress, sp, rm, rmSpeed);

    if (active == 1 && !m_editingSequence.rootMotionCurve.empty())
    {
        ImGui::TextDisabled("※ 上のシーンビューでキャラが実際に前進する量として確認できます。");
    }
}
