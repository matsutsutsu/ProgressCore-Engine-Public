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
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Game/Logics/Combat/CombatRosterComponent.h"
#include "Engine/Physics/JoltIntegration/PhysicsLayers.h" // 攻撃レイヤー選択(LayerNames)

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
    AnimSequence *m_seq = nullptr;

    // キャッシュバッファ: [0] はマスタートラック(Duration)、[1]以降がイベント
    mutable std::vector<std::pair<int, int>> m_frameCacheMap;

    AnimationSequenceImpl() = default;

    void SetSequence(AnimSequence *seq)
    {
        m_seq = seq;
    }

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

    // UI上でドラッグされたフレームを実データ(秒)に同期する
    void FlushFrameCache() const
    {
        if (!m_seq || m_frameCacheMap.empty())
            return;

        // マスタートラックのドラッグを無効化し、アセット本来の長さ(duration)に強制ロックする
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

        // 注意: ここにあった SyncFrameCache() を削除しました！
        // 毎フレーム呼ばれる Get() 内でキャッシュを再生成するとドラッグ操作が破綻するためです。

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

        if (type == (int)AnimEventType::Sound)
            ev.stringParam = "Cue_Default";
        if (type == (int)AnimEventType::Effect)
            ev.stringParam = "VFX_Default";
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

// ===================================================================================
// ===================================================================================
// AnimationSequencerWindow メンバ関数群の実装
// ===================================================================================
AnimationSequencerWindow::AnimationSequencerWindow()
    : EditorWindow("Sequencer")
{
    m_sequencerImpl = std::make_unique<AnimationSequenceImpl>();

    // 初期状態バインド
    m_sequencerImpl->SetSequence(&m_editingSequence);
    m_sequencerImpl->SyncFrameCache();

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
    ResetStateOnEntitySwitch(context);

    // 2. セーブ・ロード処理
    DrawFileIO();

    // 3. アニメーション選択UI
    char nameBuf[128];
    strcpy_s(nameBuf, sizeof(nameBuf), m_editingSequence.sequenceName.c_str());
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputText("Sequence Name", nameBuf, sizeof(nameBuf)))
        m_editingSequence.sequenceName = nameBuf;

    ImGui::SameLine();

    // ロード直後の同期 (JSONのtargetAnimNameからコンボボックスの選択位置を復元する)
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

        // アセットの長さに強制同期
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

        // アセット本来の長さにシーケンスの最大長を強制ロック
        m_editingSequence.duration = (std::max)(0.01f, animations[m_selectedAnimIndex].secondsLength);

        m_sequencerImpl->SyncFrameCache();
        m_fitCurveView = true;
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

void AnimationSequencerWindow::ResetStateOnEntitySwitch(EditorContext &context)
{
    if (context.selectedEntity != m_lastSelectedEntity)
    {
        // 解除対象は「切替前」のエンティティ。選択中の Animator へ復元すると、
        // 旧エンティティの override が残ったまま、新エンティティに別モデルの台本が
        // 差し込まれる（m_lastSelectedEntity はまだ更新前＝古い方を指している）。
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

void AnimationSequencerWindow::DrawFileIO()
{
    // Duration はアセットから自動取得されるため、テキスト表示(閲覧専用)に変更
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

                // 保存直後にランタイム側キャッシュを同一インスタンスへ上書き再読込。
                //   これをしないと ResourceManager が起動時の古い内容を返し続け、
                //   ゲーム再生に編集が反映されない（＝「保存したのに変わらない」の原因）。
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
                    nlohmann::json j = nlohmann::json::parse(text);
                    m_editingSequence = j.get<AnimSequence>();
                    m_savePath        = loadPath;

                    // ロード完了時にアニメーションリストとの同期を促すフラグ
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

void AnimationSequencerWindow::DrawEditModeToggle(EditorContext &context, AnimatorComponent *animator)
{
    if (context.isAnimEditMode)
    {
        if (!m_wasEditMode)
        {
            m_savedSequence       = animator->currentSequence;
            m_wasEditMode         = true;
            context.animEditTime = animator->currentTimer;
        }
        animator->isEditorOverride = true;
        animator->currentSequence  = &m_editingSequence;

        // 「今フレームも編集モードを維持した」印。これが途切れたフレームを
        // OnFrameBegin が検出して解除する（ウィンドウを閉じた/畳んだ場合の保険）。
        m_editingEntity     = context.selectedEntity;
        m_editModeRefreshed = true;
    }
    else if (m_wasEditMode)
    {
        ReleaseEditMode(context, context.selectedEntity);
    }
}

// -----------------------------------------------------------------------------------
// 編集モードの解除（唯一の出口）。役割は AnimationSequencerWindow.h を参照。
// -----------------------------------------------------------------------------------
void AnimationSequencerWindow::ReleaseEditMode(EditorContext &context, CCL::ECS::EntityID target)
{
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

    m_rootMotionActive     = false;
    m_savedSequence        = nullptr;
    m_wasEditMode          = false;
    m_isPlaying            = false;
    m_editingEntity        = CCL::ECS::InvalidEntityID;
    m_editModeRefreshed    = false;
    context.isAnimEditMode = false;
}

// -----------------------------------------------------------------------------------
// 描画されなかったフレームの検出（ウィンドウを閉じた/畳んだ/選択を外した）。
// -----------------------------------------------------------------------------------
void AnimationSequencerWindow::OnFrameBegin(EditorContext &context)
{
    if (m_editingEntity == CCL::ECS::InvalidEntityID)
        return;

    // 直前のフレームで DrawContents が維持していれば、まだ編集は生きている。
    const bool maintained = m_editModeRefreshed;
    m_editModeRefreshed   = false;
    if (maintained)
        return;

    // 維持されなかった＝もう画面に出ていない。ここで解除しないと isEditorOverride が
    // 残り、対象エンティティのアニメ・アニメ遷移・ルートモーションが全部止まったままになる。
    ReleaseEditMode(context, m_editingEntity);
}

void AnimationSequencerWindow::DrawRootMotionPreview(EditorContext &context, AnimatorComponent * /*animator*/)
{
    if (!context.isAnimEditMode || !context.world) return;

    TransformComponent *trans = context.world->GetComponent<TransformComponent>(context.selectedEntity);
    if (!trans) return;

    const CurveData &rmc = m_editingSequence.rootMotionCurve;
    if (rmc.empty())
    {
        // カーブが無い（または全キー削除された）→ プレビュー駆動を終了し、
        // 次にキーが追加された瞬間の位置を新しい基準点として扱う。
        m_rootMotionActive = false;
        return;
    }

    const float dur      = m_editingSequence.duration;
    const float progress = (dur > 0.0f) ? std::clamp(context.animEditTime / dur, 0.0f, 1.0f) : 0.0f;

    if (!m_rootMotionActive)
    {
        // このフレームで初めてカーブ駆動プレビューが有効になった → 現在位置を基準点として記録する。
        // 以後は毎フレーム「基準点 + 前方 * (curve(progress) - curve(0))」で絶対位置を再計算する。
        // ループするたびに m_rootMotionBasePos へ1周ぶんの前進を畳み込むので、
        // ループ境界で原点にスナップバックせず連続して前進する。
        // m_rootMotionOriginPos は累積しない「真の開始位置」で、編集モード終了時の復元に使う。
        m_rootMotionOriginPos = trans->position;
        m_rootMotionBasePos   = trans->position;
        m_rootMotionActive    = true;
    }

    // ワールド前方ベクトル（worldMatrixのZ軸列。回転はキャッシュ済みのものを使う）
    DirectX::XMFLOAT3 fwd = trans->GetForward();

    const float dist = rmc.Evaluate(progress) - rmc.Evaluate(0.0f);

    DirectX::XMFLOAT3 newPos = {
        m_rootMotionBasePos.x + fwd.x * dist,
        m_rootMotionBasePos.y + fwd.y * dist,
        m_rootMotionBasePos.z + fwd.z * dist,
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
                // ループ設定はプレビュー専用トグルを優先（アニメ設定と独立に試せる）
                if (m_loopPreview)
                {
                    context.animEditTime = std::fmod(context.animEditTime, dur);

                    // ルートモーションプレビューのループ連続化:
                    //   ここは「再生による折り返し」でのみ通る（スクラブでは通らない）ため、
                    //   1周ぶんの前進量を基準位置へ畳み込む。これにより次ループの進捗0が
                    //   前ループ終端と一致し、原点へのスナップバックが消える。
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
                    m_isPlaying           = false; // 非ループは最後で停止
                }
            }
            animator->currentTimer = context.animEditTime;
        }

        // ── 再生コントロール群 ──
        if (ImGui::Button("|<")) // 先頭へ
        {
            context.animEditTime   = 0.0f;
            animator->currentTimer = 0.0f;
            m_isPlaying             = false;
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
            m_isPlaying             = false;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("最後へ (End)");
        ImGui::SameLine();

        // ループトグル
        ImGui::Checkbox("Loop", &m_loopPreview);
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
        m_isPlaying = false;
        ImGui::TextDisabled("Enable 'Edit Mode' to preview & scrub.");
    }

    // ── Homeキー/Endキーでも先頭・末尾へ ──
    if (context.isAnimEditMode && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Home))
        {
            context.animEditTime   = 0.0f;
            animator->currentTimer = 0.0f;
            m_isPlaying             = false;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_End))
        {
            context.animEditTime   = dur;
            animator->currentTimer = dur;
            m_isPlaying             = false;
        }
    }

    // ── スナップ設定 ──
    ImGui::Checkbox("Snap", &m_snapEnabled);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::DragInt("##snapStep", &m_snapStep, 1, 1, 30, "%df");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("スナップ単位（フレーム）");
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // イベント追加
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

    int curFrame  = context.isAnimEditMode ? SecToFrame(context.animEditTime) : SecToFrame(animator->currentTimer);
    int prevFrame = curFrame;

    m_expanded    = true;
    bool changed = ImSequencer::Sequencer(
        m_sequencerImpl.get(), &curFrame, &m_expanded, &m_selectedEntry, &m_firstFrame,
        ImSequencer::SEQUENCER_EDIT_STARTEND | ImSequencer::SEQUENCER_ADD |
            ImSequencer::SEQUENCER_DEL | ImSequencer::SEQUENCER_CHANGE_FRAME);

    // ── ドラッグ後のスナップ適用（選択イベントのフレーム境界を丸める）──
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

    // 削除処理
    if (m_selectedEntry > 0 && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Delete))
    {
        m_sequencerImpl->Del(m_selectedEntry);
        m_selectedEntry = -1;
    }

    // ナッジ(微調整)処理
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

    // curFrame != prevFrame は「ImSequencerウィジェット内部の量子化/丸め」だけでも
    //   毎フレーム発生しうる（ユーザーが何も触っていなくても）。これを無条件に
    //   「ユーザーがスクラブした」とみなして animEditTime を整数フレームへ強制スナップし
    //   さらに m_isPlaying を止めてしまうと、再生中に不規則な停止と時間巻き戻り/早送りが
    //   起き、体感で「速度が速くなったり遅くなったりする」不具合になっていた。
    //   本物のドラッグ操作（左マウスボタン押下中）のときだけ反応するようにし、
    //   通常再生中はスムーズな時間進行（上の m_isPlaying ブロック）を唯一の権威にする。
    if (curFrame != prevFrame && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        m_isPlaying             = false;
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
    if (m_selectedEntry == 0 || m_selectedEntry == -1)
    {
        if (m_selectedEntry == 0)
        {
            ImGui::TextColored(ImVec4(0.5f, 1, 0.5f, 1), "Master Track (Duration)");
            ImGui::TextDisabled("Modify the total length of the sequence.");
        }

        // アセットロック済みなので閲覧のみ
        ImGui::Text("Duration: %.2f s", m_editingSequence.duration);
        ImGui::Checkbox("Edit Mode (Preview)", &context.isAnimEditMode);
    }
    else if (m_selectedEntry > 0 && m_selectedEntry <= (int)m_editingSequence.events.size())
    {
        auto &e  = m_editingSequence.events[m_selectedEntry - 1]; // イベントのインデックスは-1
        bool  ch = false;

        ImGui::TextColored(ImVec4(0.5f, 1, 0.5f, 1), "Selected Event");
        ImGui::Spacing();
        ImGui::TextDisabled("Type: %s", m_sequencerImpl->GetItemTypeName((int)e.type));
        // ── 開始/終了を秒で直接入力（ドラッグより精密）──
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

        // 武器タグ選択（CombatRoster があればプリセット、無ければ自由入力）。HitBox / Trail 共用。
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

            // 攻撃レイヤー（＝誰に当てるか）。剛体レイヤーと同じ LayerNames から選ぶ。
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
            m_sequencerImpl->SyncFrameCache();
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
    if (ImGui::Button("全体表示##cv")) m_fitCurveView = true;
    ImGui::SameLine();
    ImGui::TextDisabled("[ダブルクリック=キー追加 / 右orホイール=パン&ズーム / Alt+ハンドル=接線分離]");

    m_editingSequence.speedCurve.visible      = showSpeed;
    m_editingSequence.rootMotionCurve.visible = showRoot;

    const float dur      = m_editingSequence.duration;
    const float progress = (dur > 0.0f) ? std::clamp(animTime / dur, 0.0f, 1.0f) : 0.0f;

    // 編集対象の切り替え（選択が毎フレーム消えないよう、変化時だけ SetActiveCurve）
    int active = m_curveWidget.ActiveCurveIndex();
    int prev   = active;
    ImGui::RadioButton("速度を編集", &active, 0);
    ImGui::SameLine();
    ImGui::RadioButton("ルートモーションを編集", &active, 1);
    if (active != prev) m_curveWidget.SetActiveCurve(active);

    std::vector<CurveData *> targets = {&m_editingSequence.speedCurve, &m_editingSequence.rootMotionCurve};

    // ================================================================
    // ルートモーション編集時専用ツール
    //   カーブは「進捗→累積前進距離(m)」。0から形を作るのは接線をいじる必要があって
    //   面倒なので、プリセットボタンで単調増加の綺麗な形をワンクリックで用意する。
    //   移動距離そのものは、他のキーと同じくキャンバス/インスペクタで末尾キーの
    //   「値」を直接ドラッグして決める（専用の距離入力は用意しない＝操作がひとつで済む）。
    // ================================================================
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
                k0.interp     = CurveInterp::Cubic;
                k0.tangentOut = TangentMode::Flat;
                k1.interp     = CurveInterp::Cubic;
                k1.tangentIn  = TangentMode::Auto;
                break;
            case 2: // イーズアウト（勢いよく出て減速）
                k0.interp     = CurveInterp::Cubic;
                k0.tangentOut = TangentMode::Auto;
                k1.interp     = CurveInterp::Cubic;
                k1.tangentIn  = TangentMode::Flat;
                break;
            case 3: // スムーズ（S字：加速→減速）
                k0.interp     = CurveInterp::Cubic;
                k0.tangentOut = TangentMode::Flat;
                k1.interp     = CurveInterp::Cubic;
                k1.tangentIn  = TangentMode::Flat;
                break;
            }
            c.keys.push_back(k0);
            c.keys.push_back(k1);
            c.AssignMissingIds();
            c.RecalculateTangents();
        };

        // プリセット生成時の距離は「今のカーブの総移動量」を引き継ぐ（無ければ1.0）。
        // 生成後、末尾キーの値を通常通りドラッグすれば距離を変えられる。
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

        ImGui::TextDisabled("移動距離は末尾キーの「値」を右のインスペクタかキャンバスでドラッグして決めます。");
    }

    CurveEditorWidget::Options opt;
    opt.height        = 240.0f;
    opt.showInspector = true;
    opt.showPlayhead  = true;
    opt.playheadX     = progress;
    opt.fitRequested  = m_fitCurveView;
    m_fitCurveView     = false;

    m_curveWidget.Draw("##seqCurves", targets, opt);

    // 現在の再生位置での評価値を即読み出し（数値でも確認できる）
    const float sp = m_editingSequence.speedCurve.empty() ? 1.0f : m_editingSequence.speedCurve.Evaluate(progress);
    const float rm = m_editingSequence.rootMotionCurve.empty() ? 0.0f : m_editingSequence.rootMotionCurve.Evaluate(progress);

    // ルートモーションの瞬間速度（傾き）も出す：実際の動きの速さが直感的に分かる
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
