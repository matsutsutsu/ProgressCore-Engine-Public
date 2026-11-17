#include "Editor/Windows/Game/FormationEditorWindow.h"

#include "Engine/Platform/Core/Dialog.h"
#include "Engine/Platform/Core/FileSystem.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h"
#include "Game/Logics/AI/Drone/Formation/Data/FormationAsset.h"
#include "Game/Logics/AI/Drone/Formation/Data/FormationAssetIO.h"
#include "Game/Logics/AI/Drone/Formation/Data/FormationAssetMeta.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include "Game/Logics/AI/Drone/Formation/Data/DataDrivenFormation.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

#include <imgui.h>

#include "ImSequencer.h"

#include "Editor/Windows/Game/FormationLayerSequence.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"

using namespace CCL::AI::Formation;
using CCL::AI::Formation::Data::DataDrivenFormation;

// ---------------------------------------------------------------------------
// 編集中の陣形
// ---------------------------------------------------------------------------

DataDrivenFormation *FormationEditorWindow::DataAt(DroneFormationType slot) const
{
    return m_registry ? dynamic_cast<DataDrivenFormation *>(m_registry->Get(slot)) : nullptr;
}



FormationEditorWindow::Asset *FormationEditorWindow::CurrentAsset() const
{
    auto *data = DataAt(m_slot);
    return data ? &data->GetMutableAsset() : nullptr;
}



// 注意を引く文字の色。「未保存」「ボスAIは停止中」「置いても効かない組み合わせ」で共通
constexpr ImVec4 kWarnColor(1.0f, 0.75f, 0.2f, 1.0f);

// ファイルのパスを入れる文字列の項目と、選ぶときに使う FileDialog のプリセット
//
// パスは手で打たせない。綴りを1文字間違えるとモデルが読めず、格子の形に戻るだけで原因に気づきにくい
// パスの項目を足すときは、ここへ1行足せば「参照...」ボタン付きで描かれる
struct PathField
{
    const char *jsonName; // FieldDescriptor::jsonName
    const char *preset;   // FileDialog::OpenPreset の用途キー
};
constexpr PathField kPathFields[] = {
    {"modelPath", "Model"},
};

// fd がパスの項目なら、そのプリセットを返す
static const char *FindPathPreset(const FieldDescriptor &fd)
{
    if (fd.kind != FieldKind::String || fd.jsonName == nullptr) return nullptr;
    for (const PathField &pf : kPathFields)
        if (std::strcmp(pf.jsonName, fd.jsonName) == 0) return pf.preset;
    return nullptr;
}



// パスの項目を「今のパス（読み取り専用）＋参照...」で描く。選び直したら true
static bool DrawPathField(const FieldDescriptor &fd, void *obj, const char *preset)
{
    auto &path = *reinterpret_cast<std::string *>(static_cast<char *>(obj) + fd.offset);

    bool changed = false;
    if (ImGui::Button("参照..."))
    {
        if (auto picked = FileDialog::OpenPreset(preset))
        {
            path    = FileDialog::ConvertToRelativePath(*picked);
            changed = true;
        }
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(path.empty() ? "(未設定)" : path.c_str());
    if (!path.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", path.c_str());
    return changed;
}



FormationEditorWindow::FormationEditorWindow()
    : EditorWindow("Formation Editor", false), m_sequence(std::make_unique<FormationLayerSequence>())
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

    const Asset *asset = CurrentAsset();
    if (!asset) return;

    if (m_playing)
    {
        // 本番と同じ速さで流す。攻撃は0.4秒ほどで終わるので、目で追うためのスローも持つ
        // 振り切った形を少し見せてから頭へ戻して繰り返す（毎回押し直すのは確認にならない）
        constexpr float kHoldSeconds = 0.5f;
        m_time += ImGui::GetIO().DeltaTime * (m_slowMotion ? 0.25f : 1.0f);
        if (m_time > asset->telegraphSeconds + asset->attackSeconds + kHoldSeconds) m_time = 0.0f;
    }

    // 本編と同じ順番で見せる。予告の間は撃つ合図を落として待機させ（向きは追い続け、当たらない）、
    // 予告が終わったら合図を立てて攻撃へ進む
    const bool  telegraph = (m_time < asset->telegraphSeconds);
    const float progress  = telegraph ? 0.0f : std::min(1.0f, (m_time - asset->telegraphSeconds) / asset->attackSeconds);

    const BossHandles boss = FindBoss(context);

    // ボスBTを止める。止めないと次のフレームに requestFormation を
    // 書き戻され、押しても元の技へ戻る
    if (boss.bt) boss.bt->debugPaused = true;
    if (boss.cmd)
    {
        boss.cmd->requestFormation  = m_slot;
        boss.cmd->droneAttackActive = !telegraph;
    }

    // 時間を固定する。陣形が毎フレーム書くフィールドなので、外から固定するにはここを通す
    if (auto *data = DataAt(m_slot))
        data->SetPreview(telegraph ? m_time : asset->telegraphSeconds, progress);
}



void FormationEditorWindow::Unstage(EditorContext &context)
{
    if (!m_staged) return;
    m_staged  = false;
    m_playing = false;

    // 固定していた時間を全データ陣形ぶん解除する。選択を切り替えながら掌握を
    // 続けた場合、前の陣形に上書き値が残るため1枠ずつ見る
    for (size_t slot = 0; slot < kDroneFormationSlotCount; ++slot)
    {
        if (auto *data = DataAt(static_cast<DroneFormationType>(slot))) data->ClearPreview();
    }

    const BossHandles boss = FindBoss(context);
    if (boss.bt) boss.bt->debugPaused = false;
    if (boss.cmd) boss.cmd->droneAttackActive = false; // 攻撃信号は落とす。形はBTが次に指示する
}



FormationEditorWindow::BossHandles FormationEditorWindow::FindBoss(EditorContext &context) const
{
    BossHandles handles;
    if (!context.world) return handles;

    for (auto boss : context.world->View<BossCommandComponent>())
    {
        handles.bt  = context.world->GetComponent<BehaviorTreeComponent>(boss);
        handles.cmd = context.world->GetComponent<BossCommandComponent>(boss);
        break; // 1v1 前提。最初の1体だけ
    }
    return handles;
}



void FormationEditorWindow::OnFrameBegin(EditorContext &context)
{
    // 陣形の実体は World ごとにある（FormationRegistry.h）ので、毎フレーム今の World から引き直す
    // シーンが変わったら、前のシーンで付けた未保存の印は捨てる。その変更は作り直しで消えている
    FormationRegistry *registry = context.world ? FormationRegistry::Find(*context.world) : nullptr;
    if (registry != m_registry)
    {
        m_registry = registry;
        m_dirtySlots.clear();
    }

    // 閉じられた・畳まれたフレームでもここは走る。掌握したままウィンドウを閉じると
    // ボスBTが止まったままになるので、見えなくなった時点で必ず解く
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

    // 同名のファイルを黙って上書きすると、別の技が消える
    const std::string path = std::string("Assets/Formations/") + fileName + ".json";
    if (FileSystem::Exists(path))
    {
        m_status = "同じ名前のファイルがあります : " + path;
        return false;
    }

    auto   &registry = *m_registry; // CurrentAsset が見つかった時点で必ずある
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
        m_status = "空き枠がありません（DroneFormationType.h の枠を増やしてください）";
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
    m_status = "複製しました : " + copy.sourcePath;
    return true;
}



// ---------------------------------------------------------------------------
// 表を描く
// ---------------------------------------------------------------------------

bool FormationEditorWindow::DrawFields(const std::vector<FieldDescriptor> &fields, void *obj)
{
    // 同じ表を動きの層ごとに何度も描くので、ID は描く対象のアドレスで分ける
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
            // 入力欄だけでなく表示名にマウスを乗せても説明を出す（こちらの方が狙いやすい）
            if (fd.tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", fd.tooltip);

            ImGui::TableSetColumnIndex(1);
            if (const char *preset = FindPathPreset(fd))
            {
                changed |= DrawPathField(fd, obj, preset);
            }
            else
            {
                ImGui::SetNextItemWidth(-FLT_MIN);
                changed |= ComponentMetaImGui::DrawField(fd, obj);
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::PopID();
    return changed;
}



// ---------------------------------------------------------------------------
// 緩急の型
// ---------------------------------------------------------------------------
// 型は CurveData のキーとして動きの層へ書き込む。評価器は型を知らず、カーブを引くだけ
// どの型から作ったかは CurveData::name に id を入れて覚える（一覧で今の型を選択状態にするため）

namespace
{
    // 緩急の型ひとつ。キーは「進み 0〜1 → 動きの進み 0〜1」を描く
    struct EasePreset
    {
        const char   *id; // CurveData::name に入れる。JSON に残るので英字
        const char   *label;
        const char   *tooltip;
        int           keyCount; // 0 = キーなし（評価器の既定 t*t）
        CurveKeyframe keys[3];
    };

    constexpr EasePreset kEasePresets[] = {
        {"EaseIn", "だんだん速く（既定）", "溜めてから速くなる。振り下ろす・たたきつける動きに", 0, {}},

        {"Linear", "直線", "最初から最後まで同じ速さ", 2,
         {{.time = 0.0f, .value = 0.0f, .interp = CurveInterp::Linear},
          {.time = 1.0f, .value = 1.0f, .interp = CurveInterp::Linear}}},

        {"EaseOut", "だんだん遅く", "勢いよく出て、止まる前に減速する。構え直し・引き戻しに", 2,
         {{.time = 0.0f, .value = 0.0f, .outTangent = 2.0f},
          {.time = 1.0f, .value = 1.0f}}},

        {"EaseInOut", "ゆっくり始めてゆっくり止まる", "両端がなめらか。形を組み替えるときに", 2,
         {{.time = 0.0f, .value = 0.0f},
          {.time = 1.0f, .value = 1.0f}}},

        {"Anticipate", "溜めてから一気に", "一度少し逆へ引いてから、一気に振り切る。予備動作が目で読める", 3,
         {{.time = 0.0f, .value = 0.0f},
          {.time = 0.35f, .value = -0.12f}, // 少し逆へ引く
          {.time = 1.0f, .value = 1.0f, .inTangent = 3.0f}}},

        {"Overshoot", "行き過ぎて戻る", "終わりの値を少し越えてから戻る。止まった瞬間に重さが出る", 3,
         {{.time = 0.0f, .value = 0.0f, .outTangent = 2.5f},
          {.time = 0.7f, .value = 1.1f}, // 終わりの値を1割越える
          {.time = 1.0f, .value = 1.0f}}},
    };

    // エルミート補間で両端の傾きを 0 と 2 にすると、ちょうど評価器の既定 t*t になる
    // 「既定の形から手で直し始める」ための出発点
    constexpr CurveKeyframe kEaseInKeys[] = {
        {.time = 0.0f, .value = 0.0f},
        {.time = 1.0f, .value = 1.0f, .inTangent = 2.0f},
    };

    constexpr const char *kCustomEaseId = "Custom"; // カーブエディタで手を入れたもの
}

CurveData FormationEditorWindow::MakeEaseCurve(const char *id, const CurveKeyframe *keys, int count)
{
    CurveData curve;
    curve.name = id;
    for (int k = 0; k < count; ++k)
    {
        CurveKeyframe key = keys[k];

        // 接線は手動で持たせる。自動のままだと、読み込み時の接線の再計算で形が変わる
        key.tangent = TangentMode::Manual;
        key.id      = curve.GenId();
        curve.keys.push_back(key);
    }
    return curve;
}



void FormationEditorWindow::DrawEaseGraph(const Layer &layer)
{
    constexpr float kWidth  = 240.0f;
    constexpr float kHeight = 80.0f;
    constexpr float kMinY   = -0.2f; // 溜め・行き過ぎが枠からはみ出さないよう上下に余白を取る
    constexpr float kMaxY   = 1.2f;

    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + kWidth, p0.y + kHeight);
    const auto   toY = [&](float v) { return p1.y - (v - kMinY) / (kMaxY - kMinY) * kHeight; };

    ImDrawList *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(p0, p1, IM_COL32(30, 30, 34, 255));
    draw->AddLine(ImVec2(p0.x, toY(0.0f)), ImVec2(p1.x, toY(0.0f)), IM_COL32(255, 255, 255, 50));
    draw->AddLine(ImVec2(p0.x, toY(1.0f)), ImVec2(p1.x, toY(1.0f)), IM_COL32(255, 255, 255, 50));

    constexpr int kSamples = 48;
    ImVec2        points[kSamples + 1];
    for (int i = 0; i <= kSamples; ++i)
    {
        const float t = static_cast<float>(i) / kSamples;
        points[i]     = ImVec2(p0.x + t * kWidth, toY(DataDrivenFormation::Ease(layer, t)));
    }
    draw->PushClipRect(p0, p1, true);
    draw->AddPolyline(points, kSamples + 1, IM_COL32(120, 220, 255, 255), ImDrawFlags_None, 2.0f);
    draw->PopClipRect();

    ImGui::Dummy(ImVec2(kWidth, kHeight));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("横 : 1つの席が動き始めてから動き切るまで\n"
                          "縦 : 下の線 = 始まりの値 / 上の線 = 終わりの値");
}



bool FormationEditorWindow::DrawEase(Layer &layer)
{
    bool changed = false;

    // 今の型。キーがなければ既定、name が一覧にあればその型、どれでもなければ手で直した形
    int current = -1;
    for (int i = 0; i < IM_ARRAYSIZE(kEasePresets); ++i)
    {
        const EasePreset &preset = kEasePresets[i];
        if (preset.keyCount == 0 ? layer.curve.empty() : (!layer.curve.empty() && layer.curve.name == preset.id))
        {
            current = i;
            break;
        }
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("緩急");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(240.0f);
    if (ImGui::BeginCombo("##ease", current >= 0 ? kEasePresets[current].label : "手で直した形"))
    {
        for (int i = 0; i < IM_ARRAYSIZE(kEasePresets); ++i)
        {
            const EasePreset &preset = kEasePresets[i];
            if (ImGui::Selectable(preset.label, i == current))
            {
                layer.curve  = MakeEaseCurve(preset.id, preset.keys, preset.keyCount);
                m_curveLayer = nullptr; // カーブエディタの表示範囲を新しい形に合わせ直す
                changed      = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", preset.tooltip);
        }
        ImGui::EndCombo();
    }

    DrawEaseGraph(layer);

    // 細かい形はアニメーションと同じカーブエディタで直す。既定（キーなし）は直す対象のキーが
    // ないので、同じ形のキーを置いてから始める
    if (ImGui::TreeNode("形を手で直す"))
    {
        if (layer.curve.empty())
        {
            if (ImGui::Button("既定の形から直し始める"))
            {
                layer.curve  = MakeEaseCurve(kCustomEaseId, kEaseInKeys, IM_ARRAYSIZE(kEaseInKeys));
                m_curveLayer = nullptr;
                changed      = true;
            }
        }
        else
        {
            CurveEditorWidget::Options options;
            options.height       = 200.0f;
            options.fitRequested = (m_curveLayer != &layer);
            m_curveLayer         = &layer;

            if (m_curveWidget.Draw("##easeCurve", {&layer.curve}, options))
            {
                layer.curve.name = kCustomEaseId;
                changed          = true;
            }
        }
        ImGui::TreePop();
    }

    return changed;
}



// ---------------------------------------------------------------------------
// 動きの層
// ---------------------------------------------------------------------------

bool FormationEditorWindow::DrawLayers(Asset &asset)
{
    DrawPlayback(asset);

    bool changed = DrawTimeline(asset);
    changed |= DrawLayerButtons(asset);

    if (asset.layers.empty()) return changed;
    changed |= DrawLayerDetail(asset.layers[m_selectedLayer]);
    return changed;
}



void FormationEditorWindow::DrawPlayback(const Asset &asset)
{
    ImGui::BeginDisabled(!m_staged);
    if (ImGui::Button(m_playing ? "停止" : "再生", ImVec2(70, 0))) m_playing = !m_playing;
    ImGui::SameLine();
    if (ImGui::Button("頭に戻す", ImVec2(80, 0)))
    {
        m_time    = 0.0f;
        m_playing = false;
    }
    ImGui::SameLine();
    ImGui::Checkbox("1/4 倍", &m_slowMotion);
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (!m_staged)
        ImGui::TextDisabled("右の「実機に出す」を ON にすると形が動く");
    else if (m_time < asset.telegraphSeconds)
        ImGui::Text("予告 %.2f / %.2f 秒", m_time, asset.telegraphSeconds);
    else
        ImGui::Text("攻撃 %.2f / %.2f 秒", std::min(m_time - asset.telegraphSeconds, asset.attackSeconds), asset.attackSeconds);
}



bool FormationEditorWindow::DrawLayerButtons(Asset &asset)
{
    bool changed = false;

    // 追加・並べ替え・削除は「選んでいる動き」に対して行う。層が減った直後でも
    // 指している先がいるように、ここで選択を範囲内へ入れ直す
    const int count         = static_cast<int>(asset.layers.size());
    m_selectedLayer         = std::clamp(m_selectedLayer, 0, std::max(0, count - 1));
    const bool hasSelection = (count > 0);

    if (ImGui::Button("+ 動きを足す"))
    {
        // 既定値（始まりも終わりも 0）のままだと、足しても形が動かず効いているか分からない
        // 足した瞬間に実機で見て取れるよう、全体を 5m 持ち上げる動きで始める
        FormationLayer &added = asset.layers.emplace_back();
        added.type            = LayerType::Translate;
        added.axis            = AssetAxis::Up;
        added.to              = 5.0f;
        m_selectedLayer       = count;
        changed               = true;
    }

    ImGui::SameLine();
    if (ImGui::Button("+ 震えを足す"))
    {
        // 予告の区間いっぱいに、全席をだんだん強くざわつかせる震えを置く
        // 振れ幅を 0 から始めるので、既定の緩急(p*p)で撃つ直前ほど強くなる
        FormationLayer &added = asset.layers.emplace_back();
        added.type            = LayerType::Shiver;
        added.axis            = AssetAxis::Right;
        added.to              = 0.6f;
        added.leadThreshold   = 1.0f;
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

    return changed;
}



bool FormationEditorWindow::DrawLayerDetail(Layer &layer)
{
    char title[64];
    std::snprintf(title, sizeof(title), "選んでいる動き : %d. %s / %s", m_selectedLayer + 1,
                  Meta::kLayerTypeNames[static_cast<int>(layer.type)],
                  Meta::kAxisNames[static_cast<int>(layer.axis)]);
    ImGui::SeparatorText(title);

    bool changed = DrawFields(Meta::LayerFields(), &layer);

    // 数値としては正しいが「置いても何も起きない」組み合わせ。画面からは気づけないので言葉で出す
    if (layer.type == LayerType::Shiver)
    {
        ImGui::TextDisabled("Shiver は予告の区間に置く震え。撃つ合図で攻撃が始まると止まる");
        if (layer.from == 0.0f && layer.to == 0.0f)
            ImGui::TextColored(kWarnColor, "振れ幅が始まりも終わりも 0 なので震えない");
    }
    else if (layer.from == layer.to)
        ImGui::TextColored(kWarnColor, "始まりと終わりが同じなので、この動きは形を変えない");

    changed |= DrawEase(layer);
    return changed;
}



// ---------------------------------------------------------------------------
// 時間軸
// ---------------------------------------------------------------------------

bool FormationEditorWindow::DrawTimeline(Asset &asset)
{
    ImGui::TextDisabled("左 : 予告 %.2f 秒（震えだけ）  |  右 : 攻撃 %.2f 秒", asset.telegraphSeconds, asset.attackSeconds);

    // 帯を1本も置かなくても、この設定が ON だと予告の間ずっと全機が震える
    // 予告の震えを探すのはこのタイムラインなので、「見せ方」の中ではなくここにも出す
    ImGui::SameLine();
    bool changed = ImGui::Checkbox("全体のざわつき", &asset.output.useShiverTelegraph);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("予告の間、帯とは別に全機を共通の震えでざわつかせる（「見せ方」の「撃つ前に震わせる」と同じ設定）\n"
                          "切ると、置いた Shiver の帯の震えだけになる");

    // ImSequencer は動きが1つもないと何も描かない
    if (asset.layers.empty())
    {
        ImGui::TextDisabled("動きがありません（待機の形のまま）");
        return changed;
    }

    int       frame  = FormationLayerSequence::TimeToFrame(asset, m_time);
    const int before = frame;
    m_sequence->Begin(asset, frame);

    // ImSequencer は残りの高さを全部使うので、行数ぶんの高さの子ウィンドウに閉じ込める
    //   目盛り(20) + 行(20 x 数) + 下の横スクロール(14) + 内側の余白(8) + 少しの余裕
    constexpr float kItemHeight = 20.0f;
    const float     height      = kItemHeight * (asset.layers.size() + 1) + 14.0f + 8.0f + 4.0f;

    ImGui::BeginChild("timeline", ImVec2(0.0f, height));
    ImSequencer::Sequencer(m_sequence.get(), &frame, nullptr, &m_selectedLayer, &m_firstFrame,
                           ImSequencer::SEQUENCER_EDIT_STARTEND | ImSequencer::SEQUENCER_CHANGE_FRAME);
    ImGui::EndChild();

    ImGui::TextDisabled("帯の端 : 伸び縮み / 帯の中 : 丸ごと移動 / 上の目盛り : 再生位置 / Ctrl+ホイール : 拡大");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("帯 = この動きが働く時間帯\n"
                          "斜線1本 = 1つの席（列・段）が動き始めてから動き切るまで\n"
                          "帯が明るい間 = 今この動きが形を動かしている\n"
                          "目盛りの数字はコマ番号（左 20 コマが予告、右 50 コマが攻撃）");

    if (frame != before)
    {
        m_time    = FormationLayerSequence::FrameToTime(asset, frame);
        m_playing = false;
    }

    changed |= m_sequence->Flush();
    return changed;
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

    // 名前のある枠（縦割り等）も空き枠も、データ陣形なら全部並べる
    for (size_t slot = 0; slot < kDroneFormationSlotCount; ++slot)
    {
        const auto type = static_cast<DroneFormationType>(slot);
        auto      *data = DataAt(type);
        if (!data) continue;

        // 未保存の技には * を付ける。### 以降が ID なので、* が付いても選択は外れない
        const bool        dirty = m_dirtySlots.count(type) > 0;
        const std::string label = data->GetAsset().name + (dirty ? " *" : "") + "###" + std::to_string(slot);
        if (ImGui::Selectable(label.c_str(), type == m_slot))
        {
            m_slot          = type;
            m_selectedLayer = 0;
            m_status.clear();

            // 掌握中に選び直したら、そのまま新しい陣形を出す（「実機に出す」を押し直させない）
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
            m_dirtySlots.erase(m_slot);
            m_status = "保存しました : " + asset->sourcePath;
        }
        else
        {
            // 失敗したら未保存のままにする。消したと思って閉じられると編集が消える
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
            m_dirtySlots.erase(m_slot);
            m_status = "ファイルから読み直しました";
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("編集中の内容を捨てて、保存済みのファイルの状態へ戻す。");

    if (m_dirtySlots.count(m_slot) > 0)
    {
        ImGui::SameLine();
        ImGui::TextColored(kWarnColor, "未保存");
    }

    ImGui::Separator();

    // 画面の主役は「動き」。評価器も「待機の形を作る → 動きを順にかける」の2段なので、
    // 待機の形（割付・形・配置）は1つにまとめ、見出しに結果だけを出して既定では畳む
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

    if (changed) m_dirtySlots.insert(m_slot);
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
                          "動きは「再生」かタイムラインの目盛りで見る。OFF にすると元へ戻る。");

    if (m_staged)
        ImGui::TextColored(kWarnColor, "ボスAIは停止中");
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
