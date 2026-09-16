// 陣形エディタの実装。FormationRegistry が持つ実体を直接書き換える。
#include "FormationEditorWindow.h"

#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h"
#include "Game/Logics/AI/Drone/Formation/FormationAsset.h"
#include "Game/Logics/AI/Drone/Formation/FormationAssetIO.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include "Game/Logics/AI/Drone/Formation/Formations_DataDriven.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"

#include <algorithm>
#include <cstdio>

#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif

using namespace CCL::AI::Formation;
using CCL::AI::Formation::Data::DataDrivenFormation;

#ifdef ENABLE_EDITOR
namespace
{
// ---------------------------------------------------------------------------
// 画面に出す言葉と、内部の値の対応表
// ---------------------------------------------------------------------------

// 形。内部では「列の軸・中央揃え・列の起点」の3項目だが、既存の全技は
// この3通りのどれかに収まる。3項目を別々に出すと組み合わせを自分で考えることになり、
// 何から始めればいいか分からなくなる。
enum class ShapeKind
{
    WallSide,    // 壁：横に敷く（刃・縦割り）
    WallDepth,   // 壁：前後に敷く（挟み込み2段）
    Arm,         // 腕：前へ伸ばす（薙ぎ払い・挟み込み1段）
};
constexpr const char *kShapeNames[] = {
    "壁：横に敷く", "壁：前後に敷く", "腕：前へ伸ばす"};

// 動き。内部の「種類 × 軸」を、見たままの動作1つに畳んである。
// 「Rotate の軸が Right」から「前へ倒れる」を頭の中で復元させない。
struct MotionKind
{
    LayerType type;
    AssetAxis axis;
};
constexpr MotionKind kMotions[] = {
    {LayerType::Rotate, AssetAxis::Right},      // 前へ倒す
    {LayerType::Rotate, AssetAxis::Up},         // 横へ振る
    {LayerType::Rotate, AssetAxis::Forward},    // 横へ倒す
    {LayerType::Translate, AssetAxis::Right},   // 横へ動かす
    {LayerType::Translate, AssetAxis::Up},      // 上下に動かす
    {LayerType::Translate, AssetAxis::Forward}, // 前後に動かす
    {LayerType::Shiver, AssetAxis::Right},      // 横に震わせる
    {LayerType::Shiver, AssetAxis::Up},         // 上下に震わせる
    {LayerType::Shiver, AssetAxis::Forward},    // 前後に震わせる
};
constexpr const char *kMotionNames[] = {
    "前へ倒す", "横へ振る", "横へ倒す",
    "横へ動かす", "上下に動かす", "前後に動かす",
    "横に震わせる（予告）", "上下に震わせる（予告）", "前後に震わせる（予告）"};
constexpr int kMotionCount = static_cast<int>(std::size(kMotions));

int MotionIndexOf(const FormationLayer &layer)
{
    for (int i = 0; i < kMotionCount; ++i)
        if (kMotions[i].type == layer.type && kMotions[i].axis == layer.axis) return i;
    return 0;
}

// 動き方。内部の「ずらす基準(None/Column/Row/Group) × 逆順」を1つの選択肢に畳む。
// 2つの項目を見比べて頭の中で合成させない。
struct OrderKind
{
    PhaseSource source;
    bool        reverse;
};
constexpr OrderKind kOrders[] = {
    {PhaseSource::None, false},
    {PhaseSource::Column, false}, {PhaseSource::Column, true},
    {PhaseSource::Row, false},    {PhaseSource::Row, true},
    {PhaseSource::Group, false},  {PhaseSource::Group, true},
};
constexpr int kOrderCount = static_cast<int>(std::size(kOrders));

// 列の並ぶ向きで言葉が変わる。横に敷いた壁の「列」は左右だが、
// 前後に敷いた壁の「列」は手前と奥になる。
const char *OrderName(int index, AssetAxis columnAxis)
{
    const bool depth = (columnAxis == AssetAxis::Forward);
    switch (index)
    {
    case 0:  return "全機いっせいに";
    case 1:  return depth ? "手前から順に" : "左から順に";
    case 2:  return depth ? "奥から順に"   : "右から順に";
    case 3:  return "低いほうから順に";
    case 4:  return "高いほうから順に";
    case 5:  return "下の段から順に";
    case 6:  return "上の段から順に";
    default: return "?";
    }
}

int OrderIndexOf(const FormationLayer &layer)
{
    for (int i = 0; i < kOrderCount; ++i)
        if (kOrders[i].source == layer.phaseSource && kOrders[i].reverse == layer.phaseReverse)
            return i;
    return 0;
}

constexpr const char *kLookNames[] = {
    "動きに合わせる", "プレイヤーのほうを向く", "内側を向く（左右で向かい合う）"};
constexpr const char *kChargeNames[] = {
    "向きを決めて直進する", "形を保ったまま動く", "バネで遅れて追いつく"};

constexpr const char *kTemplateNames[] = {
    "壁を作って倒す（縦割り系）",
    "腕を伸ばして薙ぐ（横なぎ系）",
    "左右から挟む（挟み込み系）",
    "空から作る",
};

ShapeKind ShapeOf(const FormationLayout &layout)
{
    if (!layout.centerColumns) return ShapeKind::Arm;
    return (layout.columnAxis == AssetAxis::Forward) ? ShapeKind::WallDepth : ShapeKind::WallSide;
}

void ApplyShape(FormationLayout &layout, ShapeKind kind)
{
    switch (kind)
    {
    case ShapeKind::WallSide:
        layout.columnAxis = AssetAxis::Right;  layout.centerColumns = true;  break;
    case ShapeKind::WallDepth:
        layout.columnAxis = AssetAxis::Forward; layout.centerColumns = true; break;
    case ShapeKind::Arm:
        layout.columnAxis = AssetAxis::Forward; layout.centerColumns = false;
        // 腕は「ボスから離れた位置」から伸びる。0 のままだとボスに刺さる。
        if (layout.baseA <= 0.0f) layout.baseA = 3.0f;
        break;
    }
}
} // namespace
#endif // ENABLE_EDITOR

// ---------------------------------------------------------------------------

FormationEditorWindow::Asset *FormationEditorWindow::CurrentAsset() const
{
    auto *data = dynamic_cast<DataDrivenFormation *>(FormationRegistry::Instance().Get(m_slot));
    return data ? &data->GetMutableAsset() : nullptr;
}

#ifdef ENABLE_EDITOR

void FormationEditorWindow::Help(const char *title, const char *body)
{
    if (!ImGui::IsItemHovered()) return;
    m_helpTitle = title;
    m_helpBody  = body;
}

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

    auto *asset = CurrentAsset();
    if (!asset) return;

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
// 新規作成
// ---------------------------------------------------------------------------

bool FormationEditorWindow::CreateFromTemplate(int templateIndex, const char *fileName)
{
    auto &registry = FormationRegistry::Instance();

    uint8_t freeSlot = 0;
    for (uint8_t slot = kDataFormationSlotFirst; slot <= kDataFormationSlotLast; ++slot)
    {
        if (!registry.Has(static_cast<DroneFormationType>(slot))) { freeSlot = slot; break; }
    }
    if (freeSlot == 0)
    {
        m_status = "空き枠がありません（DroneComponent.h の予約枠を増やしてください）";
        return false;
    }

    Asset asset;
    asset.slot       = freeSlot;
    asset.name       = fileName;
    asset.sourcePath = std::string("Assets/Formations/") + fileName + ".json";

    // 雛形は「出発点」であって完成形ではない。ここから層を足す・消す・並べ替えると
    // 別の技になる。プリセットを選ぶだけのUIに退行させないための線引き。
    FormationLayer layer;
    switch (templateIndex)
    {
    case 0: // 壁を作って倒す
        ApplyShape(asset.layout, ShapeKind::WallSide);
        asset.layout.spacingA = asset.layout.spacingB = 2.5f;
        asset.origin.forward = 5.0f;
        asset.origin.up      = 2.0f;
        asset.attackSeconds          = 0.4000f;
        layer.type = LayerType::Rotate; layer.axis = AssetAxis::Right;
        layer.from = 0.0f; layer.to = 109.0f;
        asset.layers.push_back(layer);
        break;

    case 1: // 腕を伸ばして薙ぐ
        ApplyShape(asset.layout, ShapeKind::Arm);
        asset.assign.rows  = 3;
        asset.layout.baseA = 4.0f; asset.layout.spacingA = 2.0f;
        asset.layout.baseB = 1.5f; asset.layout.spacingB = 1.5f;
        asset.attackSeconds        = 0.6250f;
        layer.type = LayerType::Rotate; layer.axis = AssetAxis::Up;
        layer.from = 100.0f; layer.to = -100.0f;
        asset.layers.push_back(layer);
        break;

    case 2: // 左右から挟む
        ApplyShape(asset.layout, ShapeKind::Arm);
        asset.assign.sideGroups = 2;
        asset.assign.split      = AssetSplit::Interleaved;
        asset.assign.rows       = 2;
        asset.layout.baseA = 3.0f; asset.layout.spacingA = 1.8f;
        asset.layout.baseB = 0.8f; asset.layout.spacingB = 1.5f;
        asset.attackSeconds        = 0.3333f;
        asset.output.look  = LookMode::ToPlayer;
        layer.type = LayerType::Rotate; layer.axis = AssetAxis::Up;
        layer.from = 86.0f; layer.to = 0.0f; layer.mirror = true;
        asset.layers.push_back(layer);
        layer = FormationLayer{};
        layer.type = LayerType::Translate; layer.axis = AssetAxis::Right;
        layer.from = 0.0f; layer.to = 0.8f; layer.mirror = true;
        asset.layers.push_back(layer);
        break;

    default: // 空から
        ApplyShape(asset.layout, ShapeKind::WallSide);
        asset.origin.forward = 5.0f;
        asset.origin.up      = 2.0f;
        break;
    }

    if (!FormationAssetIO::Save(asset.sourcePath, asset))
    {
        m_status = "作成に失敗しました（コンソールを確認）";
        return false;
    }

    registry.Register<DataDrivenFormation>(static_cast<DroneFormationType>(freeSlot))
        ->SetAsset(asset);

    m_slot   = static_cast<DroneFormationType>(freeSlot);
    m_dirty  = false;
    m_status = "作成しました : " + asset.sourcePath;
    return true;
}

bool FormationEditorWindow::DuplicateCurrent(const char *fileName)
{
    const Asset *src = CurrentAsset();
    if (!src) return false;

    auto &registry = FormationRegistry::Instance();

    uint8_t freeSlot = 0;
    for (uint8_t slot = kDataFormationSlotFirst; slot <= kDataFormationSlotLast; ++slot)
    {
        if (!registry.Has(static_cast<DroneFormationType>(slot))) { freeSlot = slot; break; }
    }
    if (freeSlot == 0)
    {
        m_status = "空き枠がありません";
        return false;
    }

    Asset copy       = *src;
    copy.slot        = freeSlot; // 枠は技の識別子。複製で同じ番号を名乗らせない
    copy.name        = fileName;
    copy.sourcePath  = std::string("Assets/Formations/") + fileName + ".json";

    if (!FormationAssetIO::Save(copy.sourcePath, copy))
    {
        m_status = "複製に失敗しました（コンソールを確認）";
        return false;
    }

    registry.Register<DataDrivenFormation>(static_cast<DroneFormationType>(freeSlot))
        ->SetAsset(copy);

    m_slot   = static_cast<DroneFormationType>(freeSlot);
    m_dirty  = false;
    m_status = "複製しました : " + copy.sourcePath;
    return true;
}

// ---------------------------------------------------------------------------
// 3ペイン
// ---------------------------------------------------------------------------

void FormationEditorWindow::DrawContents(EditorContext &context)
{
    constexpr float kListWidth = 170.0f;
    constexpr float kViewWidth = 280.0f;

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
    ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "陣形");
    ImGui::Separator();

    auto &registry = FormationRegistry::Instance();
    for (uint8_t slot = kDataFormationSlotFirst; slot <= kDataFormationSlotLast; ++slot)
    {
        const auto type = static_cast<DroneFormationType>(slot);
        auto *data = dynamic_cast<DataDrivenFormation *>(registry.Get(type));
        if (!data) continue;

        const bool selected = (type == m_slot);
        if (ImGui::Selectable(data->GetAsset().name.c_str(), selected))
        {
            m_slot  = type;
            m_dirty = false;
            m_status.clear();

            // 掌握中に選び直したら、そのまま新しい陣形を出す。いちいち
            // 「実機に出す」を押し直させない。
            if (m_staged) PushStage(context);
        }
    }

    ImGui::Separator();

    if (ImGui::Button("+ 新しく", ImVec2(-1, 0))) ImGui::OpenPopup("新しい陣形");
    if (ImGui::Button("複製", ImVec2(-1, 0)))
    {
        if (CurrentAsset()) ImGui::OpenPopup("陣形の複製");
    }

    if (ImGui::BeginPopupModal("新しい陣形", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("どんな技から始めますか？");
        ImGui::Combo("##tpl", &m_newTemplate, kTemplateNames,
                     static_cast<int>(std::size(kTemplateNames)));
        ImGui::TextDisabled("雛形は出発点です。あとから動きを足す・消す・\n"
                            "並べ替えると別の技になります。");
        ImGui::Spacing();
        ImGui::InputText("ファイル名", m_newFileName, sizeof(m_newFileName));
        ImGui::TextDisabled("Assets/Formations/%s.json に保存されます", m_newFileName);

        ImGui::Spacing();
        if (ImGui::Button("作成", ImVec2(100, 0)))
        {
            if (CreateFromTemplate(m_newTemplate, m_newFileName)) ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("やめる", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

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
        ImGui::EndPopup();
    }
}

void FormationEditorWindow::DrawEditPane(EditorContext &context)
{
    Asset *asset = CurrentAsset();
    if (!asset)
    {
        ImGui::TextWrapped("左の「+ 新しく」から陣形を作ってください。");
        return;
    }

    // 名前とファイル。名前は BT の候補にも出るので、ここで直せるようにしておく。
    char nameBuf[64];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", asset->name.c_str());
    ImGui::SetNextItemWidth(-140.0f);
    if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf)))
    {
        asset->name = nameBuf;
        m_dirty     = true;
    }
    Help("陣形の名前", "左の一覧と、ボスBTの候補に出る名前。ファイル名とは別。");

    ImGui::SameLine();
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
    Help("保存", "編集した内容をファイルへ書き出す。保存前に検証を通すので、\n"
                 "保存できたものは必ず読み直せる。");

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
    Help("戻す", "編集中の内容を捨てて、保存済みのファイルの状態へ巻き戻す。");

    if (m_dirty)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "未保存");
    }

    ImGui::Separator();

    DrawShapeSection(*asset, context);
    ImGui::Spacing();
    DrawPlacementSection(*asset);
    ImGui::Spacing();
    DrawMotionSection(*asset);
    ImGui::Spacing();
    DrawSpeedSection(*asset);
    ImGui::Spacing();
    DrawCombatSection(*asset);
}

void FormationEditorWindow::DrawShapeSection(Asset &asset, EditorContext &context)
{
    ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "並べ方");

    int shape = static_cast<int>(ShapeOf(asset.layout));
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::Combo("形", &shape, kShapeNames, static_cast<int>(std::size(kShapeNames))))
    {
        ApplyShape(asset.layout, static_cast<ShapeKind>(shape));
        m_dirty = true;
    }
    Help("形",
         "壁：横に敷く    ボスの正面に立つ一枚の壁。縦割りの刃はこれ。\n"
         "壁：前後に敷く  奥行き方向に長い壁。挟み込み2段の左右の壁。\n"
         "腕：前へ伸ばす  ボスから前へ伸びる一本の線。回す動きと組むと薙ぎ払いになる。");

    if (ShapeOf(asset.layout) == ShapeKind::Arm)
    {
        ImGui::SetNextItemWidth(120.0f);
        m_dirty |= ImGui::DragFloat("根元の距離", &asset.layout.baseA, 0.05f, 0.0f, 50.0f, "%.2f m");
        Help("根元の距離", "腕の内側の端をボスからどれだけ離すか。0 にするとボスに刺さる。");
    }

    ImGui::SetNextItemWidth(120.0f);
    m_dirty |= ImGui::DragInt("1列の機数", &asset.assign.rows, 0.1f, 0, 32,
                              asset.assign.rows <= 0 ? "自動" : "%d");
    Help("1列の機数",
         "1本の列に何機を縦に積むか。0 にすると機数から正方形に近い格子を自動で組む。\n"
         "3 にすれば「高さ3機の列」が横にずらりと並ぶ。");

    int cols = 1, rows = 1;
    DataDrivenFormation::GridSize(asset, CountDrones(context), cols, rows);
    ImGui::TextDisabled("  -> %d 列 x %d 行", cols, rows);

    m_dirty |= ImGui::DragFloat("列の間隔", &asset.layout.spacingA, 0.05f, 0.0f, 20.0f, "%.2f m");
    Help("列の間隔",
         "隣り合う列の距離。ドローンは画面上でおよそ3mに見えるので、\n"
         "3m を切ると1枚の塊に潰れて「1000機の個別制御」が読めなくなる。");

    m_dirty |= ImGui::DragFloat("段の間隔", &asset.layout.spacingB, 0.05f, 0.0f, 20.0f, "%.2f m");
    Help("段の間隔", "同じ列の中で、上へ積むときの距離。");

    m_dirty |= ImGui::DragFloat("下端の高さ", &asset.layout.baseB, 0.05f, -20.0f, 50.0f, "%.2f m");
    Help("下端の高さ", "形のいちばん下の高さ。回転の軸はここではなく「置く場所」で決まる。");

    ImGui::Spacing();

    bool twoSides = (asset.assign.sideGroups > 1);
    if (ImGui::Checkbox("左右2つに分ける", &twoSides))
    {
        asset.assign.sideGroups = twoSides ? 2 : 1;
        m_dirty                 = true;
    }
    Help("左右2つに分ける",
         "機体を2組に分け、動きの向きを左右で反転できるようにする。\n"
         "挟み込みのように「2本の腕が対称に閉じる」技に使う。\n"
         "分けると、各レイヤーに「左右で対称に動かす」が出る。");

    if (twoSides)
    {
        bool interleaved = (asset.assign.split == AssetSplit::Interleaved);
        if (ImGui::Checkbox("1機ずつ交互に配る", &interleaved))
        {
            asset.assign.split = interleaved ? AssetSplit::Interleaved : AssetSplit::Block;
            m_dirty            = true;
        }
        Help("配り方",
             "ON   0,1,0,1, と1機ずつ左右へ配る\n"
             "OFF  前半を左、後半を右にまとめて配る\n"
             "撃破で機体が減ったときの崩れ方が変わる。交互のほうが左右が均等に減る。");
    }

    bool twoTiers = (asset.assign.tierGroups > 1);
    if (ImGui::Checkbox("上下2段に分ける", &twoTiers))
    {
        asset.assign.tierGroups = twoTiers ? 2 : 1;
        m_dirty                 = true;
    }
    Help("上下2段に分ける",
         "機体を上下2組に分ける。段ごとに動き出す時刻をずらせるようになり、\n"
         "「下の段が先に閉じて、あとから上の段が来る」が作れる。\n"
         "分けると、各レイヤーの「動き方」に段の選択肢が出る。");

    if (twoTiers)
    {
        m_dirty |= ImGui::DragFloat("段の高さ差", &asset.layout.tierStepB, 0.05f, -20.0f, 20.0f, "%.2f m");
        Help("段の高さ差", "下の段と上の段の高さの差。プレイヤーのジャンプ高さと\n"
                           "合わせて「跳んで避ける／伏せて避ける」を決める。");
    }
}

void FormationEditorWindow::DrawPlacementSection(Asset &asset)
{
    ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "置く場所（ボスから）");

    m_dirty |= ImGui::DragFloat("前へ", &asset.origin.forward, 0.05f, -50.0f, 50.0f, "%.2f m");
    m_dirty |= ImGui::DragFloat("高さ", &asset.origin.up, 0.05f, -20.0f, 50.0f, "%.2f m");
    m_dirty |= ImGui::DragFloat("横へ", &asset.origin.right, 0.05f, -50.0f, 50.0f, "%.2f m");
    Help("置く場所",
         "形をボスからどれだけずらして置くか。\n"
         "回す動きはこの点を軸に回るので、ここが「蝶番の位置」になる。\n"
         "縦割りの「高さ 2.0」がそのまま蝶番の高さ。軸の位置と回す量が\n"
         "別々になっているので、片方だけ調整できる。");
}

void FormationEditorWindow::DrawMotionSection(Asset &asset)
{
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "動き（上から順に重ねる）");
    ImGui::SameLine();
    if (ImGui::SmallButton("+ 追加")) ImGui::OpenPopup("動きを追加");

    if (ImGui::BeginPopup("動きを追加"))
    {
        for (int i = 0; i < kMotionCount; ++i)
        {
            if (!ImGui::Selectable(kMotionNames[i])) continue;

            FormationLayer layer;
            layer.type = kMotions[i].type;
            layer.axis = kMotions[i].axis;
            if (layer.type == LayerType::Shiver) layer.to = 0.5f;
            asset.layers.push_back(layer);
            m_dirty = true;
        }
        ImGui::EndPopup();
    }

    if (asset.layers.empty())
    {
        ImGui::TextDisabled("  動きがありません（形が止まったままになります）");
        return;
    }

    // 削除と並べ替えはループの後で反映する。走査中に vector を触ると
    // 次の周回が別の要素を指す。
    int removeIndex = -1, moveIndex = -1, moveDelta = 0;

    for (int i = 0; i < static_cast<int>(asset.layers.size()); ++i)
    {
        FormationLayer &layer = asset.layers[i];
        ImGui::PushID(i);

        char header[96];
        std::snprintf(header, sizeof(header), "%d. %s", i + 1,
                      kMotionNames[MotionIndexOf(layer)]);

        const bool open = ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen);

        ImGui::SameLine(ImGui::GetContentRegionMax().x - 78.0f);
        if (ImGui::SmallButton("^")) { moveIndex = i; moveDelta = -1; }
        Help("順番を上げる", "動きは上から順に重なる。順番を変えると別の技になる。\n"
                             "「倒してから上げる」と「上げてから倒す」は着地点が違う。");
        ImGui::SameLine();
        if (ImGui::SmallButton("v")) { moveIndex = i; moveDelta = +1; }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
        if (ImGui::SmallButton("x")) removeIndex = i;
        ImGui::PopStyleColor();

        if (open)
        {
            ImGui::Indent();
            DrawLayerBody(asset, layer);
            ImGui::Unindent();
        }
        ImGui::PopID();
    }

    if (moveIndex >= 0)
    {
        const int target = moveIndex + moveDelta;
        if (target >= 0 && target < static_cast<int>(asset.layers.size()))
        {
            std::swap(asset.layers[moveIndex], asset.layers[target]);
            m_dirty = true;
        }
    }
    if (removeIndex >= 0)
    {
        asset.layers.erase(asset.layers.begin() + removeIndex);
        m_dirty = true;
    }
}

void FormationEditorWindow::DrawLayerBody(Asset &asset, Layer &layer)
{
    int motion = MotionIndexOf(layer);
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::Combo("動き", &motion, kMotionNames, kMotionCount))
    {
        layer.type = kMotions[motion].type;
        layer.axis = kMotions[motion].axis;
        m_dirty    = true;
    }
    Help("動き",
         "形をどう加工するか。\n"
         "前へ倒す      置く場所を蝶番にして、上へ伸びた形を前へ倒す\n"
         "横へ振る      縦の軸まわりに形ごと回す（薙ぎ払い）\n"
         "横へ動かす    形を保ったまま平行移動する（挟む・広げる）\n"
         "震わせる      撃つ前だけ小さく揺らす。次に来る側を読ませる予告");

    if (layer.type == LayerType::Shiver)
    {
        m_dirty |= ImGui::DragFloat("揺れ幅", &layer.to, 0.01f, 0.0f, 5.0f, "%.2f m");
        Help("揺れ幅", "撃つ前の小刻みな揺れの大きさ。");

        m_dirty |= ImGui::DragFloat("揺れの速さ", &layer.frequency, 0.1f, 0.5f, 30.0f, "%.1f");
        Help("揺れの速さ", "上げすぎるとノイズに見えて予告として読めなくなる。");

        const int shiverOrder = OrderIndexOf(layer);
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::BeginCombo("どれが揺れるか", OrderName(shiverOrder, asset.layout.columnAxis)))
        {
            for (int i = 0; i < kOrderCount; ++i)
            {
                if (kOrders[i].source == PhaseSource::Group && asset.assign.tierGroups <= 1)
                    continue;
                if (ImGui::Selectable(OrderName(i, asset.layout.columnAxis), i == shiverOrder))
                {
                    layer.phaseSource  = kOrders[i].source;
                    layer.phaseReverse = kOrders[i].reverse;
                    m_dirty            = true;
                }
            }
            ImGui::EndCombo();
        }
        Help("どれが揺れるか",
             "最初に動く側だけを揺らして「次はこっちから来る」を読ませる。\n"
             "全機が揺れると、どちらが先か分からなくなって予告の意味が消える。");

        m_dirty |= ImGui::DragFloat("揺れる範囲", &layer.leadThreshold, 0.01f, 0.0f, 1.0f, "%.2f");
        Help("揺れる範囲", "先頭から何割までを揺らすか。0.18 なら先頭の2割弱だけ。");

        ImGui::TextDisabled("撃つ合図が出るまでの間だけ効きます");
        return;
    }

    const char *unit = (layer.type == LayerType::Rotate) ? "%.1f 度" : "%.2f m";
    m_dirty |= ImGui::DragFloat("はじめ", &layer.from, 0.5f, -720.0f, 720.0f, unit);
    m_dirty |= ImGui::DragFloat("おわり", &layer.to, 0.5f, -720.0f, 720.0f, unit);
    Help("はじめ / おわり",
         "撃つ前の値と、振り切ったときの値。\n"
         "「はじめ」が待機中にプレイヤーへ見せる形になる。ここを読んで避ける。");

    // 動き方。ずらす基準と逆順を1つに畳んである。
    const AssetAxis columnAxis = asset.layout.columnAxis;
    const bool      hasTiers   = (asset.assign.tierGroups > 1);

    int order = OrderIndexOf(layer);
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::BeginCombo("動き方", OrderName(order, columnAxis)))
    {
        for (int i = 0; i < kOrderCount; ++i)
        {
            // 段に分けていないのに段の選択肢を出すと、選んでも何も起きない。
            if (kOrders[i].source == PhaseSource::Group && !hasTiers) continue;

            if (ImGui::Selectable(OrderName(i, columnAxis), i == order))
            {
                layer.phaseSource  = kOrders[i].source;
                layer.phaseReverse = kOrders[i].reverse;

                // 「いっせいに」へ戻したら、ずれ幅も戻す。残っていると
                // 全機同時のはずなのに動きが速くなる。
                if (kOrders[i].source == PhaseSource::None)
                    layer.duration = layer.windowEnd - layer.windowBegin;

                m_dirty = true;
            }
        }
        ImGui::EndCombo();
    }
    Help("動き方",
         "全機いっせいに  全部が同時に動く\n"
         "左から順に      左端から順に動き出して、波のように伝わる\n"
         "下の段から順に  段ごとに時間差をつける（先攻・後攻）\n"
         "\n"
         "順番にすると「1つが動き切る長さ」が出る。そこで波の細さを決める。");

    if (layer.phaseSource != PhaseSource::None)
    {
        m_dirty |= ImGui::DragFloat("1つが動き切る長さ", &layer.duration, 0.01f, 0.05f, 1.0f, "%.2f");
        Help("1つが動き切る長さ",
             "全体を 1 としたとき、1つの列（や段）が動き切るのに使う割合。\n"
             "小さいほど順番がはっきり見える（波が細くなる）。\n"
             "1.00 にすると全機同時と同じになる。");
    }

    float window[2] = {layer.windowBegin, layer.windowEnd};
    if (ImGui::DragFloat2("動く時間帯", window, 0.01f, 0.0f, 1.0f, "%.2f"))
    {
        layer.windowBegin = window[0];
        layer.windowEnd   = window[1];
        if (layer.phaseSource == PhaseSource::None)
            layer.duration = layer.windowEnd - layer.windowBegin;
        m_dirty = true;
    }
    Help("動く時間帯",
         "技全体を 0〜1 としたとき、この動きが効く区間。\n"
         "既定は 0.00〜1.00（最初から最後まで）。\n"
         "\n"
         "動きを2つ重ねて時間帯をずらすと、多段技になる。\n"
         "  1つ目 0.00〜0.40 上へ持ち上げる\n"
         "  2つ目 0.40〜1.00 前へ倒す\n"
         "  は 振りかぶってから叩きつける");

    if (asset.assign.sideGroups > 1)
    {
        m_dirty |= ImGui::Checkbox("左右で対称に動かす", &layer.mirror);
        Help("左右で対称に動かす",
             "左右に分けた2組で、値の符号を反転する。\n"
             "「はじめ 86度 -> おわり 0度」なら、片方は +86->0、もう片方は -86->0。\n"
             "2本の腕が向かい合って閉じる動きになる。");
    }

    if (layer.curve.empty())
        ImGui::TextDisabled("動きの緩急 : 既定（はじめゆっくり、後半で加速）");
    else
        ImGui::TextDisabled("動きの緩急 : カーブ %zu 点", layer.curve.keys.size());
}

void FormationEditorWindow::DrawSpeedSection(Asset &asset)
{
    ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "速さ");

    ImGui::SetNextItemWidth(160.0f);
    m_dirty |= ImGui::DragFloat("振り切るまで", &asset.attackSeconds, 0.01f, 0.05f, 10.0f, "%.2f 秒");
    Help("振り切るまで",
         "撃つ合図が出てから、形が最後まで動き切るのにかかる秒数。\n"
         "短いほど避けにくい。ジャスト回避を成立させたいなら、\n"
         "予告（BTの待ち時間）を長く取って、ここは短くする。");
}

void FormationEditorWindow::DrawCombatSection(Asset &asset)
{
    ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "当たり方・見た目");

    FormationOutput &o = asset.output;

    int look = static_cast<int>(o.look);
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::Combo("機首の向き", &look, kLookNames, static_cast<int>(std::size(kLookNames))))
    {
        o.look  = static_cast<LookMode>(look);
        m_dirty = true;
    }
    Help("機首の向き",
         "動きに合わせる  回した動きと同じだけ機首も回る（倒れる刃は倒れる先を向く）\n"
         "プレイヤーのほう  形がどこにあってもプレイヤーを向く\n"
         "内側を向く        左右に分けた2組が向かい合う（挟み込みの壁）");

    int charge = static_cast<int>(o.chargeMode);
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::Combo("動き方（機体）", &charge, kChargeNames,
                     static_cast<int>(std::size(kChargeNames))))
    {
        o.chargeMode = static_cast<ChargeMode>(charge);
        m_dirty      = true;
    }
    Help("機体の動き方",
         "形を保ったまま動く  目標へ毎フレーム貼り付く。形が一切歪まない（壁・刃）\n"
         "向きを決めて直進    撃った瞬間の方向へまっすぐ飛ぶ（弾幕）\n"
         "バネで遅れて追いつく  速いほど後ろへ流れる（隊列）");

    m_dirty |= ImGui::Checkbox("地面より下へ行かない", &o.clampToGround);
    Help("地面より下へ行かない", "水平を越えて倒れる形が床へ潜るのを止める。");

    m_dirty |= ImGui::Checkbox("予告の震えを出す", &o.useShiverTelegraph);
    Help("予告の震え",
         "全機共通の予告（Drone Manager で調整するもの）を使うか。\n"
         "動きそのもので予告する技は切る。予告が二重になるとどちらを読むか濁る。");

    ImGui::Spacing();
    FormationCombat &c = o.combat;
    m_dirty |= ImGui::DragFloat("当たる半径", &c.hitRadius, 0.05f, 0.1f, 10.0f, "%.2f m");
    m_dirty |= ImGui::DragFloat("ダメージ", &c.damage, 0.5f, 0.0f, 200.0f, "%.1f");
    m_dirty |= ImGui::DragFloat("ノックバック", &c.knockback, 0.5f, 0.0f, 100.0f, "%.1f");
    m_dirty |= ImGui::DragFloat("ヒットストップ", &c.hitStopDuration, 0.005f, 0.0f, 1.0f, "%.3f 秒");
    Help("ヒットストップ",
         "当たった瞬間に時間を止める長さ。大技ほど長くすると重さが出るが、\n"
         "伸ばしすぎると操作が途切れる。");
}

void FormationEditorWindow::DrawViewPane(EditorContext &context)
{
    ImGui::TextColored(ImVec4(0.7f, 1.0f, 0.8f, 1.0f), "見る");
    ImGui::Separator();

    bool staged = m_staged;
    if (ImGui::Checkbox("実機に出す", &staged))
    {
        if (staged) Stage(context);
        else        Unstage(context);
    }
    Help("実機に出す",
         "編集中の陣形をその場でドローンに組ませる。同時に4つを行う。\n"
         "  1. ボスAIを凍らせる（でないと次のフレームに元の技へ戻る）\n"
         "  2. この陣形を指示する\n"
         "  3. 攻撃信号を立てる\n"
         "  4. 進行度を手送りに切り替える\n"
         "OFF にすると全部元へ戻す。");

    if (m_staged)
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "ボスAIは停止中");
    else
        ImGui::TextDisabled("ボスAIは通常どおり");

    ImGui::Spacing();

    ImGui::BeginDisabled(!m_staged);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##progress", &m_progress, 0.0f, 1.0f, "進行度 %.2f"))
        m_playing = false; // 手で掴んだら自動再生は止める
    Help("進行度",
         "技の始まり(0)から振り切り(1)までを手で送る。\n"
         "技は本来0.4秒ほどで終わるので、ここで止めないと途中の形を見られない。\n"
         "0 のままにすれば「撃つ前の待機の形」になる。");

    if (ImGui::Button(m_playing ? "停止" : "再生", ImVec2(70, 0))) m_playing = !m_playing;
    ImGui::SameLine();
    if (ImGui::Button("0 に戻す", ImVec2(80, 0))) { m_progress = 0.0f; m_playing = false; }
    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::Separator();

    if (const Asset *asset = CurrentAsset())
    {
        const int total = CountDrones(context);
        int cols = 1, rows = 1;
        DataDrivenFormation::GridSize(*asset, total, cols, rows);

        ImGui::Text("出ている機数 %d", total);
        ImGui::Text("格子         %d 列 x %d 行", cols, rows);
        ImGui::Text("待機の大きさ 幅 %.1fm  高さ %.1fm",
                    (cols - 1) * asset->layout.spacingA,
                    (rows - 1) * asset->layout.spacingB);
        ImGui::TextDisabled("枠 %u / %s", asset->slot, asset->sourcePath.c_str());
    }

    if (!m_status.empty())
    {
        ImGui::Separator();
        ImGui::TextWrapped("%s", m_status.c_str());
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "この項目について");

    if (m_helpTitle.empty())
    {
        ImGui::TextDisabled("項目にマウスを乗せると\nここに説明が出ます。");
        return;
    }
    ImGui::TextUnformatted(m_helpTitle.c_str());
    ImGui::Separator();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(m_helpBody.c_str());
    ImGui::PopTextWrapPos();
}

#else // ENABLE_EDITOR

void FormationEditorWindow::DrawContents(EditorContext &) {}
void FormationEditorWindow::OnFrameBegin(EditorContext &) {}
void FormationEditorWindow::DrawListPane(EditorContext &) {}
void FormationEditorWindow::DrawEditPane(EditorContext &) {}
void FormationEditorWindow::DrawViewPane(EditorContext &) {}
void FormationEditorWindow::DrawShapeSection(Asset &, EditorContext &) {}
void FormationEditorWindow::DrawPlacementSection(Asset &) {}
void FormationEditorWindow::DrawMotionSection(Asset &) {}
void FormationEditorWindow::DrawLayerBody(Asset &, Layer &) {}
void FormationEditorWindow::DrawSpeedSection(Asset &) {}
void FormationEditorWindow::DrawCombatSection(Asset &) {}
void FormationEditorWindow::Stage(EditorContext &) {}
void FormationEditorWindow::Unstage(EditorContext &) {}
void FormationEditorWindow::PushStage(EditorContext &) {}
bool FormationEditorWindow::CreateFromTemplate(int, const char *) { return false; }
bool FormationEditorWindow::DuplicateCurrent(const char *) { return false; }
void FormationEditorWindow::Help(const char *, const char *) {}
int  FormationEditorWindow::CountDrones(EditorContext &) const { return 0; }

#endif // ENABLE_EDITOR
