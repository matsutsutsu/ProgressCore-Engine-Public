// 陣形エディタの実装。FormationRegistry が持つ実体を直接書き換える。
#include "FormationEditorWindow.h"

#include "Engine/Platform/Core/FileSystem.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h"
#include "Game/Logics/AI/Drone/Formation/FormationAsset.h"
#include "Game/Logics/AI/Drone/Formation/FormationAssetIO.h"
#include "Game/Logics/AI/Drone/Formation/FormationAssetMeta.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include "Game/Logics/AI/Drone/Formation/Formations_DataDriven.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#ifdef ENABLE_EDITOR
#include <imgui.h>
#include <imgui_internal.h> // ImRect（ImSequencer の描画コールバックが受け取る）

#include "ImSequencer.h"

#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

using namespace CCL::AI::Formation;
using CCL::AI::Formation::Data::DataDrivenFormation;

FormationEditorWindow::Asset *FormationEditorWindow::CurrentAsset() const
{
    auto *data = dynamic_cast<DataDrivenFormation *>(FormationRegistry::Instance().Get(m_slot));
    return data ? &data->GetMutableAsset() : nullptr;
}

#ifdef ENABLE_EDITOR

// ---------------------------------------------------------------------------
// ImSequencer とのつなぎ
// ---------------------------------------------------------------------------
// ImSequencer は帯の両端を int のフレームで受け取り、ドラッグ中はそのアドレスへ直接書き込む。
// こちらの時間帯は 0〜1 の float なので、int の置き場所(m_frames)を別に持って往復させる。
//   データ → m_frames : Sync()。掴んでいる間は呼ばない（ドラッグ中の値を毎フレーム巻き戻してしまう）
//   m_frames → データ : Flush()。掴んだ帯が実際に動いたときだけ書き戻す（クリックしただけで
//                       書き戻すと、欄に打った 0.333 が刻みの 0.34 へ丸められる）

class FormationEditorWindow::LayerSequence : public ImSequencer::SequenceInterface
{
public:
    /// 0〜1 を何等分するか。攻撃が0.4秒なら1刻み8ミリ秒で、ゲームの1コマ(16ミリ秒)より細かい。
    /// ImSequencer は1刻みを既定で10pxで描くので、増やすと横スクロールが要るようになる。
    static constexpr int kFrames = 50;

    static int   ToFrame(float t) { return static_cast<int>(std::lround(t * kFrames)); }
    static float ToTime(int frame) { return static_cast<float>(frame) / kFrames; }

    Asset *asset    = nullptr;
    float  progress = 0.0f; // 帯の濃淡に使う（再生位置が帯の中にあるか）

    bool IsEditing() const { return m_editIndex >= 0; }

    void Sync()
    {
        m_frames.resize(asset->layers.size());
        for (size_t i = 0; i < asset->layers.size(); ++i)
            m_frames[i] = ToFrames(asset->layers[i]);
    }

    /// 掴んでいた帯が動いていたらデータへ書き戻す。書き戻したら true。
    bool Flush()
    {
        const int index = m_editIndex >= 0 ? m_editIndex : m_releasedIndex;
        m_releasedIndex = -1;
        if (index < 0 || index >= static_cast<int>(asset->layers.size())) return false;

        // ImSequencer は左端のはみ出しは止めるが、右端は止めない。
        auto &[start, end] = m_frames[index];
        end                = std::min(end, kFrames - 1);
        start              = std::min(start, end);

        FormationLayer &layer = asset->layers[index];
        if (m_frames[index] == ToFrames(layer)) return false;

        // 帯は「開始のコマの左端」から「終了のコマの右端」まで描かれるので、終了は +1 して戻す。
        const float oldWidth = layer.windowEnd - layer.windowBegin;
        layer.windowBegin    = ToTime(start);
        layer.windowEnd      = ToTime(end + 1);
        const float newWidth = layer.windowEnd - layer.windowBegin;

        // 動きの長さは帯の幅ではなく duration が決める（評価器は windowBegin から duration かけて進む）。
        // 帯と一緒に duration も同じ比率で伸び縮みさせないと、帯を縮めても動きは縮まない。
        // 比率で持つので、波の扇の開き方（duration / 帯の幅）はドラッグしても変わらない。
        if (oldWidth > 0.0f) layer.duration = std::min(newWidth, layer.duration * newWidth / oldWidth);
        return true;
    }

    int GetFrameMin() const override { return 0; }
    int GetFrameMax() const override { return kFrames; }
    int GetItemCount() const override { return static_cast<int>(asset->layers.size()); }

    void BeginEdit(int index) override { m_editIndex = index; }
    void EndEdit() override
    {
        m_releasedIndex = m_editIndex;
        m_editIndex     = -1;
    }

    const char *GetItemLabel(int index) const override
    {
        const FormationLayer &layer = asset->layers[index];
        std::snprintf(m_label, sizeof(m_label), "%d. %s / %s", index + 1,
                      Meta::kLayerTypeNames[static_cast<int>(layer.type)],
                      Meta::kAxisNames[static_cast<int>(layer.axis)]);
        return m_label;
    }

    void Get(int index, int **start, int **end, int *type, unsigned int *color) override
    {
        // 種類ごとの帯の色。並びは LayerType の宣言順（Rotate / Translate / Shiver）。
        constexpr ImU32 kTypeColors[] = {
            IM_COL32(80, 140, 230, 255),
            IM_COL32(90, 190, 120, 255),
            IM_COL32(230, 160, 60, 255),
        };

        const FormationLayer &layer = asset->layers[index];
        if (start) *start = &m_frames[index].first;
        if (end) *end = &m_frames[index].second;
        if (type) *type = static_cast<int>(layer.type);
        if (color)
        {
            // 再生位置が帯の中にある間だけ明るくする。今どの動きが形を動かしているかが一目で分かる。
            const bool  active = (layer.windowBegin <= progress && progress <= layer.windowEnd);
            const ImU32 base   = kTypeColors[static_cast<int>(layer.type)];
            *color             = active ? base : ((base >> 1) & 0x007F7F7F);
        }
    }

    /// 帯の上に斜線を描く。1本 = 1つの席（列・段）が動き始めてから動き切るまで。
    /// 扇状に広がっていれば「波」、1本なら全機いっせい。
    void CustomDrawCompact(int index, ImDrawList *draw, const ImRect &rc, const ImRect &clip) override
    {
        // 波の本数は目安。実際の列数を出すには割付の式をここへ書き直すことになり、評価器と二重管理になる。
        constexpr int kFanLines = 6;

        // rc は「フレーム -0.5 〜 最終フレーム +0.5」の範囲で渡される。一方で帯はコマの左端から、
        // しかも ImSequencer 内の子ウィンドウの余白(FramePadding)ぶん右へずれて描かれるので、両方を足して揃える。
        const float cell = (rc.Max.x - rc.Min.x) / (GetFrameMax() - GetFrameMin() + 1);
        const float left = rc.Min.x + ImGui::GetStyle().FramePadding.x - 0.5f * cell;
        const auto  toX  = [&](float t) { return left + t * kFrames * cell; };
        const float y0   = rc.Min.y + 4.0f;
        const float y1   = rc.Max.y - 3.0f;

        const FormationLayer &layer    = asset->layers[index];
        const int             lines    = (layer.phaseSource == PhaseSource::None) ? 1 : kFanLines;
        const float           width    = layer.windowEnd - layer.windowBegin;
        const float           duration = std::min(layer.duration, width); // 評価器と同じ抑え方
        const float           slack    = width - duration;

        draw->PushClipRect(clip.Min, clip.Max, true);
        for (int k = 0; k < lines; ++k)
        {
            // 評価器の start = windowBegin + seatT * (窓幅 - duration) と同じ式で引く。
            const float seatT = (lines == 1) ? 0.0f : static_cast<float>(k) / (lines - 1);
            const float start = layer.windowBegin + seatT * slack;
            draw->AddLine(ImVec2(toX(start), y1), ImVec2(toX(start + duration), y0), IM_COL32(255, 255, 255, 170));
        }
        draw->PopClipRect();
    }

private:
    static std::pair<int, int> ToFrames(const FormationLayer &layer)
    {
        const int start = ToFrame(layer.windowBegin);
        return {start, std::max(start, ToFrame(layer.windowEnd) - 1)};
    }

    std::vector<std::pair<int, int>> m_frames;

    int m_editIndex     = -1; // 掴んでいる帯。離すと -1
    int m_releasedIndex = -1; // 離したフレームだけ、離した帯の番号が入る

    mutable char m_label[64] = {}; // GetItemLabel の戻り値の置き場所（呼び出し側はすぐ描いて捨てる）
};

FormationEditorWindow::FormationEditorWindow()
    : EditorWindow("Formation Editor", false), m_sequence(std::make_unique<LayerSequence>())
{
}

FormationEditorWindow::~FormationEditorWindow() = default;

int FormationEditorWindow::CountDrones(EditorContext &context) const
{
    if (!context.world) return 0;
    int count = 0;
    context.world->ViewEach<DroneComponent>([&count](CCL::ECS::EntityID) { ++count; });
    return count;
}

// ---------------------------------------------------------------------------
// 実機の掌握
// ---------------------------------------------------------------------------

void FormationEditorWindow::Stage(EditorContext &context)
{
    m_staged = true;
    PushStage(context);
}

void FormationEditorWindow::PushStage(EditorContext &context)
{
    if (!m_staged || !context.world) return;

    if (m_playing)
    {
        // 自動再生。振り切ったら頭へ戻して繰り返す（毎回押し直すのは確認にならない）。
        m_progress += ImGui::GetIO().DeltaTime * 0.35f;
        if (m_progress > 1.0f) m_progress = 0.0f;
    }

    if (!CurrentAsset()) return;

    auto view = context.world->View<BossCommandComponent>();
    for (auto boss : view)
    {
        // ボスBTを凍らせる。これをしないと次のフレームに requestFormation を
        // 書き戻され、押しても元の技へ戻る。
        if (auto *bt = context.world->GetComponent<BehaviorTreeComponent>(boss))
            bt->debugPaused = true;

        if (auto *cmd = context.world->GetComponent<BossCommandComponent>(boss))
        {
            cmd->requestFormation  = m_slot;
            cmd->droneAttackActive = true;
        }
        break; // 1v1 前提。最初の1体だけ
    }

    // 進行度を握る。陣形が毎フレーム書くフィールドなので、外から握るにはここを通す。
    if (auto *data = dynamic_cast<DataDrivenFormation *>(FormationRegistry::Instance().Get(m_slot)))
        data->SetProgressOverride(m_progress);
}

void FormationEditorWindow::Unstage(EditorContext &context)
{
    if (!m_staged) return;
    m_staged  = false;
    m_playing = false;

    // 握っていた進行度を全データ陣形ぶん解放する。選択を切り替えながら掌握を
    // 続けた場合、前の陣形に上書き値が残るため1枠ずつ見る。
    for (uint8_t slot = kDataFormationSlotFirst; slot <= kDataFormationSlotLast; ++slot)
    {
        auto *data = dynamic_cast<DataDrivenFormation *>(
            FormationRegistry::Instance().Get(static_cast<DroneFormationType>(slot)));
        if (data) data->ClearProgressOverride();
    }

    if (!context.world) return;

    auto view = context.world->View<BossCommandComponent>();
    for (auto boss : view)
    {
        if (auto *bt = context.world->GetComponent<BehaviorTreeComponent>(boss))
            bt->debugPaused = false;

        if (auto *cmd = context.world->GetComponent<BossCommandComponent>(boss))
            cmd->droneAttackActive = false; // 攻撃信号は落とす。形はBTが次に指示する
        break;
    }
}

void FormationEditorWindow::OnFrameBegin(EditorContext &context)
{
    // 閉じられた・畳まれたフレームでもここは走る。掌握したままウィンドウを閉じると
    // ボスBTが凍ったままになるので、見えなくなった時点で必ず解く。
    if (!GetVisible())
    {
        Unstage(context);
        return;
    }
    PushStage(context);
}

// ---------------------------------------------------------------------------
// 複製
// ---------------------------------------------------------------------------

bool FormationEditorWindow::DuplicateCurrent(const char *fileName)
{
    const Asset *src = CurrentAsset();
    if (!src) return false;

    // 同名のファイルを黙って上書きすると、別の技が消える。
    const std::string path = std::string("Assets/Formations/") + fileName + ".json";
    if (FileSystem::Exists(path))
    {
        m_status = "同じ名前のファイルがあります : " + path;
        return false;
    }

    auto   &registry = FormationRegistry::Instance();
    uint8_t freeSlot = 0;
    for (uint8_t slot = kDataFormationSlotFirst; slot <= kDataFormationSlotLast; ++slot)
    {
        if (!registry.Has(static_cast<DroneFormationType>(slot)))
        {
            freeSlot = slot;
            break;
        }
    }
    if (freeSlot == 0)
    {
        m_status = "空き枠がありません（DroneComponent.h の予約枠を増やしてください）";
        return false;
    }

    Asset copy      = *src;
    copy.slot       = freeSlot; // 枠は技の識別子。複製で同じ番号を名乗らせない
    copy.name       = fileName;
    copy.sourcePath = path;

    if (!FormationAssetIO::Save(copy.sourcePath, copy))
    {
        m_status = "複製に失敗しました（コンソールを確認）";
        return false;
    }

    registry.Register<DataDrivenFormation>(static_cast<DroneFormationType>(freeSlot))->SetAsset(copy);

    m_slot   = static_cast<DroneFormationType>(freeSlot);
    m_dirty  = false;
    m_status = "複製しました : " + copy.sourcePath;
    return true;
}

// ---------------------------------------------------------------------------
// 表を描く
// ---------------------------------------------------------------------------

bool FormationEditorWindow::DrawFields(const std::vector<FieldDescriptor> &fields, void *obj)
{
    // 同じ表を動きの層ごとに何度も描くので、ID は描く対象のアドレスで分ける。
    ImGui::PushID(obj);

    bool changed = false;
    if (ImGui::BeginTable("fields", 2))
    {
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, 170.0f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

        for (const FieldDescriptor &fd : fields)
        {
            ImGui::PushID(fd.jsonName);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(fd.name);
            // 入力欄だけでなく表示名にマウスを乗せても説明を出す（こちらの方が狙いやすい）。
            if (fd.tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", fd.tooltip);

            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= ComponentMetaImGui::DrawField(fd, obj);

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::PopID();
    return changed;
}

bool FormationEditorWindow::DrawLayers(Asset &asset)
{
    bool changed = false;

    // 再生の操作はタイムラインの真上に置く。進行度と帯が同じ時間軸だと目で分かるように。
    ImGui::BeginDisabled(!m_staged);
    if (ImGui::Button(m_playing ? "停止" : "再生", ImVec2(70, 0))) m_playing = !m_playing;
    ImGui::SameLine();
    if (ImGui::Button("0 に戻す", ImVec2(80, 0)))
    {
        m_progress = 0.0f;
        m_playing  = false;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (m_staged)
        ImGui::Text("進行度 %.2f", m_progress);
    else
        ImGui::TextDisabled("右の「実機に出す」を ON にすると形が動く");

    changed |= DrawTimeline(asset);

    // 追加・並べ替え・削除は「選んでいる動き」に対して行う。
    const int count         = static_cast<int>(asset.layers.size());
    m_selectedLayer         = std::clamp(m_selectedLayer, 0, std::max(0, count - 1));
    const bool hasSelection = (count > 0);

    if (ImGui::Button("+ 動きを足す"))
    {
        // 既定値（始まりも終わりも 0）のままだと、足しても形が動かず効いているか分からない。
        // 足した瞬間に実機で見て取れるよう、全体を 5m 持ち上げる動きで始める。
        FormationLayer &added = asset.layers.emplace_back();
        added.type            = LayerType::Translate;
        added.axis            = AssetAxis::Up;
        added.to              = 5.0f;
        m_selectedLayer       = count;
        changed               = true;
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(!hasSelection || m_selectedLayer == 0);
    if (ImGui::ArrowButton("up", ImGuiDir_Up))
    {
        std::swap(asset.layers[m_selectedLayer], asset.layers[m_selectedLayer - 1]);
        --m_selectedLayer;
        changed = true;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!hasSelection || m_selectedLayer == count - 1);
    if (ImGui::ArrowButton("down", ImGuiDir_Down))
    {
        std::swap(asset.layers[m_selectedLayer], asset.layers[m_selectedLayer + 1]);
        ++m_selectedLayer;
        changed = true;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!hasSelection);
    if (ImGui::Button("消す"))
    {
        asset.layers.erase(asset.layers.begin() + m_selectedLayer);
        m_selectedLayer = std::max(0, m_selectedLayer - 1);
        changed         = true;
    }
    ImGui::EndDisabled();

    if (asset.layers.empty()) return changed;

    FormationLayer &layer = asset.layers[m_selectedLayer];

    char title[64];
    std::snprintf(title, sizeof(title), "選んでいる動き : %d. %s / %s", m_selectedLayer + 1,
                  Meta::kLayerTypeNames[static_cast<int>(layer.type)],
                  Meta::kAxisNames[static_cast<int>(layer.axis)]);
    ImGui::SeparatorText(title);

    changed |= DrawFields(Meta::LayerFields(), &layer);

    // 数値としては正しいが「置いても何も起きない」組み合わせ。画面からは気づけないので言葉で出す。
    constexpr ImVec4 kWarn(1.0f, 0.75f, 0.2f, 1.0f);
    if (layer.type == LayerType::Shiver)
        ImGui::TextColored(kWarn, "Shiver は撃つ前（進行度 0）の震え。時間帯は使わず、攻撃中は何もしない");
    else if (layer.from == layer.to)
        ImGui::TextColored(kWarn, "始まりと終わりが同じなので、この動きは形を変えない");

    if (layer.curve.empty())
        ImGui::TextDisabled("緩急 : 既定（p*p。溜めてから速くなる）");
    else
        ImGui::TextDisabled("緩急 : カーブ %zu 点（JSON の curve で編集）", layer.curve.keys.size());

    return changed;
}

bool FormationEditorWindow::DrawTimeline(Asset &asset)
{
    // ImSequencer は動きが1つも無いと何も描かない。
    if (asset.layers.empty())
    {
        ImGui::TextDisabled("動きがありません（待機の形のまま）");
        return false;
    }

    LayerSequence &sequence = *m_sequence;
    sequence.asset          = &asset;
    sequence.progress       = m_progress;
    if (!sequence.IsEditing()) sequence.Sync();

    int       frame  = LayerSequence::ToFrame(m_progress);
    const int before = frame;

    // ImSequencer は残りの高さを全部使うので、行数ぶんの高さの子ウィンドウに閉じ込める。
    //   目盛り(20) + 行(20 x 数) + 下の横スクロール(14) + 内側の余白(8) + 少しの余裕
    constexpr float kItemHeight = 20.0f;
    const float     height      = kItemHeight * (asset.layers.size() + 1) + 14.0f + 8.0f + 4.0f;

    ImGui::BeginChild("timeline", ImVec2(0.0f, height));
    ImSequencer::Sequencer(&sequence, &frame, nullptr, &m_selectedLayer, &m_firstFrame,
                           ImSequencer::SEQUENCER_EDIT_STARTEND | ImSequencer::SEQUENCER_CHANGE_FRAME);
    ImGui::EndChild();

    ImGui::TextDisabled("帯の端 : 伸び縮み / 帯の中 : 丸ごと移動 / 上の目盛り : 進行度 / Ctrl+ホイール : 拡大");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("帯 = この動きが働く時間帯\n"
                          "斜線1本 = 1つの席（列・段）が動き始めてから動き切るまで\n"
                          "帯が明るい間 = 今この動きが形を動かしている");

    if (frame != before)
    {
        m_progress = LayerSequence::ToTime(frame);
        m_playing  = false;
    }

    return sequence.Flush();
}

// ---------------------------------------------------------------------------
// 3ペイン
// ---------------------------------------------------------------------------

void FormationEditorWindow::DrawContents(EditorContext &context)
{
    constexpr float kListWidth = 170.0f;
    constexpr float kViewWidth = 220.0f;

    ImGui::BeginChild("list", ImVec2(kListWidth, 0), true);
    DrawListPane(context);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("edit", ImVec2(-(kViewWidth + 8.0f), 0), true);
    DrawEditPane(context);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("view", ImVec2(0, 0), true);
    DrawViewPane(context);
    ImGui::EndChild();
}

void FormationEditorWindow::DrawListPane(EditorContext &context)
{
    ImGui::SeparatorText("陣形");

    auto &registry = FormationRegistry::Instance();
    for (uint8_t slot = kDataFormationSlotFirst; slot <= kDataFormationSlotLast; ++slot)
    {
        const auto type = static_cast<DroneFormationType>(slot);
        auto      *data = dynamic_cast<DataDrivenFormation *>(registry.Get(type));
        if (!data) continue;

        if (ImGui::Selectable(data->GetAsset().name.c_str(), type == m_slot))
        {
            m_slot          = type;
            m_selectedLayer = 0;
            m_dirty         = false;
            m_status.clear();

            // 掌握中に選び直したら、そのまま新しい陣形を出す（「実機に出す」を押し直させない）。
            if (m_staged) PushStage(context);
        }
    }

    ImGui::Separator();

    ImGui::BeginDisabled(!CurrentAsset());
    if (ImGui::Button("複製して新しく作る", ImVec2(-1, 0))) ImGui::OpenPopup("陣形の複製");
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("選んでいる技を別名で複製する。近い技を選んでから押すと早い。");

    if (ImGui::BeginPopupModal("陣形の複製", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::InputText("ファイル名", m_newFileName, sizeof(m_newFileName));
        ImGui::TextDisabled("Assets/Formations/%s.json に保存されます", m_newFileName);
        ImGui::Spacing();
        if (ImGui::Button("複製する", ImVec2(100, 0)))
        {
            if (DuplicateCurrent(m_newFileName)) ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("やめる", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
        if (!m_status.empty()) ImGui::TextWrapped("%s", m_status.c_str());
        ImGui::EndPopup();
    }
}

void FormationEditorWindow::DrawEditPane(EditorContext &context)
{
    Asset *asset = CurrentAsset();
    if (!asset)
    {
        ImGui::TextWrapped("左の一覧から陣形を選んでください。");
        return;
    }

    if (ImGui::Button("保存", ImVec2(60, 0)))
    {
        if (FormationAssetIO::Save(asset->sourcePath, *asset))
        {
            m_dirty  = false;
            m_status = "保存しました : " + asset->sourcePath;
        }
        else
        {
            // 失敗したら未保存のままにする。消したと思って閉じられると編集が消える。
            m_status = "保存に失敗しました（コンソールを確認）";
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("戻す", ImVec2(60, 0)))
    {
        Asset reloaded;
        if (FormationAssetIO::Load(asset->sourcePath, reloaded))
        {
            *asset   = reloaded;
            m_dirty  = false;
            m_status = "ファイルから読み直しました";
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("編集中の内容を捨てて、保存済みのファイルの状態へ戻す。");

    if (m_dirty)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "未保存");
    }

    ImGui::Separator();

    // 画面の主役は「動き」。評価器も「待機の形を作る → 動きを順にかける」の2段なので、
    // 待機の形（割付・形・配置）は1つにまとめ、見出しに結果だけを出して既定では畳む。
    bool changed = false;

    if (ImGui::CollapsingHeader("基本", ImGuiTreeNodeFlags_DefaultOpen))
        changed |= DrawFields(Meta::RootFields(), asset);

    int cols = 1, rows = 1;
    DataDrivenFormation::GridSize(*asset, CountDrones(context), cols, rows);
    char shapeTitle[96];
    std::snprintf(shapeTitle, sizeof(shapeTitle), "待機の形 : %d 列 x %d 行 / ボスの前 %.1fm###shape",
                  cols, rows, asset->origin.forward);
    if (ImGui::CollapsingHeader(shapeTitle))
    {
        ImGui::SeparatorText("割付 : 何番の機体がどの席に着くか");
        changed |= DrawFields(Meta::AssignFields(), &asset->assign);
        ImGui::SeparatorText("形 : 席をどう並べるか");
        changed |= DrawFields(Meta::LayoutFields(), &asset->layout);
        ImGui::SeparatorText("配置 : ボスからどれだけ離すか（回転の軸の位置）");
        changed |= DrawFields(Meta::OriginFields(), &asset->origin);
    }

    if (ImGui::CollapsingHeader("動き : 上から順に待機の形へかかる", ImGuiTreeNodeFlags_DefaultOpen))
        changed |= DrawLayers(*asset);

    if (ImGui::CollapsingHeader("見せ方"))
        changed |= DrawFields(Meta::OutputFields(), &asset->output);
    if (ImGui::CollapsingHeader("当たり"))
        changed |= DrawFields(Meta::CombatFields(), &asset->output.combat);

    if (changed) m_dirty = true;
}

void FormationEditorWindow::DrawViewPane(EditorContext &context)
{
    ImGui::SeparatorText("見る");

    bool staged = m_staged;
    if (ImGui::Checkbox("実機に出す", &staged))
    {
        if (staged)
            Stage(context);
        else
            Unstage(context);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("ボスAIを止めて、この陣形をその場でドローンに組ませる。\n"
                          "進行度はタイムラインで動かす。OFF にすると元へ戻る。");

    if (m_staged)
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "ボスAIは停止中");
    else
        ImGui::TextDisabled("ボスAIは通常どおり");

    ImGui::Separator();

    if (const Asset *asset = CurrentAsset())
    {
        const int total = CountDrones(context);
        int       cols = 1, rows = 1;
        DataDrivenFormation::GridSize(*asset, total, cols, rows);

        ImGui::Text("出ている機数 %d", total);
        ImGui::Text("格子 %d 列 x %d 行", cols, rows);
        ImGui::Text("待機の大きさ 幅 %.1fm / 高さ %.1fm",
                    (cols - 1) * asset->layout.spacingA,
                    (rows - 1) * asset->layout.spacingB);
        ImGui::TextDisabled("%s", asset->sourcePath.c_str());
    }

    if (!m_status.empty())
    {
        ImGui::Separator();
        ImGui::TextWrapped("%s", m_status.c_str());
    }
}

#else // ENABLE_EDITOR

class FormationEditorWindow::LayerSequence
{
};

FormationEditorWindow::FormationEditorWindow()
    : EditorWindow("Formation Editor", false)
{
}

FormationEditorWindow::~FormationEditorWindow() = default;

void FormationEditorWindow::DrawContents(EditorContext &) {}
void FormationEditorWindow::OnFrameBegin(EditorContext &) {}
void FormationEditorWindow::DrawListPane(EditorContext &) {}
void FormationEditorWindow::DrawEditPane(EditorContext &) {}
bool FormationEditorWindow::DrawTimeline(Asset &) { return false; }
void FormationEditorWindow::DrawViewPane(EditorContext &) {}
bool FormationEditorWindow::DrawFields(const std::vector<FieldDescriptor> &, void *) { return false; }
bool FormationEditorWindow::DrawLayers(Asset &) { return false; }
void FormationEditorWindow::Stage(EditorContext &) {}
void FormationEditorWindow::Unstage(EditorContext &) {}
void FormationEditorWindow::PushStage(EditorContext &) {}
bool FormationEditorWindow::DuplicateCurrent(const char *) { return false; }
int  FormationEditorWindow::CountDrones(EditorContext &) const { return 0; }

#endif // ENABLE_EDITOR
