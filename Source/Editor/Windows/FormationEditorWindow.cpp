// 陣形(FormationSpec)の編集UIとプレビュー。
#include "FormationEditorWindow.h"

#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h" // BehaviorTreeComponent
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Game/Logics/AI/Drone/DroneFormationSystem.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"

#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"

#include <cmath>
#include <imgui.h>
#include <json.hpp>

using namespace CCL::AI::Formation;
using DirectX::SimpleMath::Vector3;

namespace
{
// プレビュー専用の場。ボスを原点、プレイヤーを正面に置く決め打ち。
//   実機と同じ IFormation を評価するので、形の作られ方は本番と一致する。
constexpr float kPreviewPlayerDistance = 20.0f;

// Vector3 の成分を軸番号で引く（0=X/右, 1=Y/上, 2=Z/前）。
float AxisValue(const Vector3 &v, int axis)
{
    return (axis == 0) ? v.x : (axis == 1) ? v.y : v.z;
}

FormationContext MakePreviewContext()
{
    FormationContext ctx;
    ctx.bossPos   = Vector3::Zero;
    ctx.playerPos = Vector3(0.0f, 0.0f, kPreviewPlayerDistance);
    ctx.time      = static_cast<float>(ImGui::GetTime()); // 予備動作の震えを動かす
    ctx.dt        = 0.0f;                                 // 進行度はスクラブが決める
    return ctx;
}

// 目盛りの間隔を「1/2/5 × 10^n」から選ぶ。等間隔だと桁が変わるたびに読めなくなる。
float NiceStep(float span)
{
    const float raw = (span > 0.0f) ? span / 6.0f : 1.0f;
    const float mag = std::pow(10.0f, std::floor(std::log10(raw)));
    const float n   = raw / mag;
    const float mul = (n < 1.5f) ? 1.0f : (n < 3.5f) ? 2.0f : (n < 7.5f) ? 5.0f : 10.0f;
    return mul * mag;
}
} // namespace

void FormationEditorWindow::Hint(const char *text)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void FormationEditorWindow::CaptureBaseline(const Spec &spec)
{
    const nlohmann::json j = spec;
    m_baseline             = j.dump();
    m_baselineTarget       = m_editTarget;
}

// ============================================================================
// 入り口
// ============================================================================
void FormationEditorWindow::DrawContents(EditorContext &context)
{
    auto &registry = FormationRegistry::Instance();

    const FormationSpecAsset *assets = DroneFormationSystem::SpecAssets();
    const int                 count  = DroneFormationSystem::SpecAssetCount();

    // ---- 編集対象 ----
    std::vector<const char *> names(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
        names[static_cast<size_t>(i)] = assets[i].name;

    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::Combo("陣形", &m_editTarget, names.data(), count))
    {
        m_abDone         = false;
        m_abMaxError     = -1.0f;
        m_baselineTarget = -1; // 対象が変わったので控えを取り直す
    }

    const DroneFormationType type      = assets[m_editTarget].type;
    Data                    *formation = registry.GetData(type);
    if (!formation)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "アセット未読み込み: %s",
                           DroneFormationSystem::SpecAssetPath(assets[m_editTarget].name).c_str());
        return;
    }

    Spec &spec = formation->spec;

    if (m_baselineTarget != m_editTarget) CaptureBaseline(spec);

    // 未保存かどうか。JSON にして比べるので、どのフィールドを足しても判定が追従する。
    const bool dirty = (nlohmann::json(spec).dump() != m_baseline);

    ImGui::SameLine();
    if (dirty)
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "● 未保存");
    else
        ImGui::TextDisabled("保存済み");

    ImGui::SameLine();
    bool preferData = registry.PreferData();
    if (ImGui::Checkbox("データ実装を使う", &preferData))
        registry.SetPreferData(preferData);
    Hint("OFF なら従来の C++ 実装が動く。\n"
         "技を出したまま ON/OFF を切り替えて、動きが変わらなければ\n"
         "このアセットが元の実装を正しく再現できている。");

    ImGui::Separator();

    // ---- 3ペイン：左=パラメータ / 右=プレビュー / 下=カーブ ----
    const float curveHeight = 220.0f;
    const float bodyHeight  = ImGui::GetContentRegionAvail().y - curveHeight
                           - ImGui::GetStyle().ItemSpacing.y * 2.0f;

    ImGui::BeginChild("##params", ImVec2(410.0f, bodyHeight), true);
    {
        DrawBossControls(context, type);
        ImGui::Separator();

        m_sideGroups = (std::max)(1, spec.assign.sideGroups);
        m_tierGroups = (std::max)(1, spec.assign.tierGroups);

        DrawAssignSection(spec);
        DrawShapeSection(spec);
        DrawTransformSection(spec);
        DrawTimingSection(spec);
        DrawTellSection(spec);
        DrawOutputSection(spec);

        ImGui::Separator();
        const std::string path = formation->assetPath.empty()
                                   ? DroneFormationSystem::SpecAssetPath(assets[m_editTarget].name)
                                   : formation->assetPath;
        DrawAssetControls(context, *formation, path);
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##preview", ImVec2(0.0f, bodyHeight), true);
    DrawPreviewPane(*formation);
    ImGui::EndChild();

    // ---- カーブ（全幅）----
    //   既存の C++ 陣形は eased = p*p 固定で、「タメてから一気に」等が書けなかった。
    //   ここが唯一その表現を作れる場所になる。
    if (spec.timing.curve.empty())
    {
        ImGui::TextDisabled("進行度 → 変換量：カーブ未設定（p×p の easeIn で動作中）");
        Hint("既存の C++ 陣形はすべて p×p 固定だった。\n"
             "カーブを作ると「最初は動かず後半で一気に」「途中で止めて見せる」\n"
             "といった、コードでは書けなかった動きが作れる。");
        if (ImGui::Button("カーブを作って編集する", ImVec2(240, 26)))
        {
            CurveData &curve = spec.timing.curve;
            curve.name       = "easeIn";
            curve.AddKey(0.0f, 0.0f);
            curve.AddKey(1.0f, 1.0f);
        }
    }
    else
    {
        std::vector<CurveData *> curves = {&spec.timing.curve};

        CurveEditorWidget::Options options;
        options.height       = curveHeight - 46.0f;
        options.showPlayhead = true;
        options.playheadX    = (m_previewProgress > 1.0f) ? 1.0f : m_previewProgress;

        m_curveWidget.Draw("##formationCurve", curves, options);

        if (ImGui::Button("カーブを消す (p×p へ戻す)", ImVec2(220, 24)))
            spec.timing.curve.keys.clear();
    }
}

// ============================================================================
// 実機連携・保存
// ============================================================================
void FormationEditorWindow::DrawBossControls(EditorContext &context, DroneFormationType type)
{
    if (!context.world) return;

    BehaviorTreeComponent *bt  = nullptr;
    BossCommandComponent  *cmd = nullptr;
    for (auto boss : context.world->View<BossCommandComponent, BehaviorTreeComponent>())
    {
        bt  = context.world->GetComponent<BehaviorTreeComponent>(boss);
        cmd = context.world->GetComponent<BossCommandComponent>(boss);
        break;
    }

    if (!bt || !cmd)
    {
        ImGui::TextDisabled("ボスが居ないシーンです（右のプレビューだけ使えます）");
        return;
    }

    // ボスAIを止める。止めないと BT が毎フレーム陣形を選び直すので、
    //   下のボタンで出した形が次のフレームに別の技へ塗り潰される。
    //   歩き回られると陣形の原点（ボス位置）も動いて形が読めないので、
    //   行動要求も併せて切る（BossActionSystem が Idle になる）。
    bool paused = bt->debugPaused;
    if (ImGui::Checkbox("ボスAIを止める", &paused))
    {
        bt->debugPaused = paused;
        if (paused) cmd->currentActionId = 0;
    }
    Hint("止めないと、ボスの behavior tree が毎フレーム別の技を選ぶので\n"
         "下のボタンで出した形がすぐ塗り潰される。\n"
         "ボスの歩行も止まるので、陣形の原点が動かなくなる。");

    if (!bt->debugPaused)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "← 先にこれを止める");
    }

    ImGui::BeginDisabled(!bt->debugPaused);
    if (ImGui::Button("① 形を作る", ImVec2(115, 26)))
    {
        cmd->requestFormation  = type;
        cmd->droneAttackActive = false;
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.3f, 0.2f, 1.0f));
    if (ImGui::Button("② 攻撃！", ImVec2(115, 26)))
    {
        cmd->requestFormation  = type;
        cmd->droneAttackActive = true;
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::Button("③ 周回へ", ImVec2(115, 26)))
    {
        cmd->requestFormation  = DroneFormationType::OrbitCircle;
        cmd->droneAttackActive = false;
    }
    ImGui::EndDisabled();
}

void FormationEditorWindow::DrawAssetControls(EditorContext &context, Data &formation, const std::string &path)
{
    const bool dirty = (nlohmann::json(formation.spec).dump() != m_baseline);

    ImGui::BeginDisabled(!dirty);
    if (ImGui::Button("保存", ImVec2(90, 26)))
    {
        const nlohmann::json j = formation.spec;
        if (FileSystem::WriteAllText(path, j.dump(2)))
        {
            CaptureBaseline(formation.spec);
            CCL_LOG_SUCCESS(LogCategory::AI, "[FormationSpec] 保存: %s", path.c_str());
        }
        else
        {
            CCL_LOG_ERROR(LogCategory::AI, "[FormationSpec] 保存に失敗: %s", path.c_str());
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("変更を破棄", ImVec2(110, 26)))
    {
        // 控えから戻す。ファイルを読み直さないので、未保存で消えるのはこの編集分だけ。
        try
        {
            formation.spec = nlohmann::json::parse(m_baseline).get<Spec>();
        }
        catch (const std::exception &e)
        {
            CCL_LOG_ERROR(LogCategory::AI, "[FormationSpec] 破棄に失敗: %s", e.what());
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("ファイルから再読込", ImVec2(150, 26)))
    {
        if (context.systemManager)
        {
            if (auto *system = context.systemManager->GetSystem<DroneFormationSystem>())
            {
                system->LoadSpecAssets();
                m_baselineTarget = -1; // 読み直したので控えも取り直す
            }
        }
    }

    ImGui::TextDisabled("%s", path.c_str());

    ImGui::Spacing();
    if (ImGui::Button("C++実装と照合する", ImVec2(180, 26)))
        RunAbCheck(context, DroneFormationSystem::SpecAssets()[m_editTarget].type);
    Hint("100機 × 進行度21点で C++ 実装と座標を突き合わせ、最大のずれを出す。\n"
         "0.01m 未満なら画面では区別が付かない＝再現できている。");

    if (m_abDone)
    {
        if (m_abMaxError < 0.0f)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "片方の実装しか無い（比較できない）");
        }
        else
        {
            const bool match = (m_abMaxError < 0.01f);
            ImGui::TextColored(match ? ImVec4(0.4f, 1.0f, 0.5f, 1.0f) : ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                               "%s  最大ずれ %.4f m", match ? "一致" : "ずれあり", m_abMaxError);
            if (!match)
                ImGui::TextDisabled("最悪 localIndex=%d / 進行度=%.2f", m_abWorstIndex, m_abWorstProgress);
        }
    }
}

void FormationEditorWindow::RunAbCheck(EditorContext &context, DroneFormationType type)
{
    auto &registry = FormationRegistry::Instance();

    m_abDone     = true;
    m_abMaxError = -1.0f;

    IFormation *native = registry.GetNative(type);
    IFormation *data   = registry.GetData(type);
    if (!native || !data) return; // 片方しか無ければ比べようがない

    // 実機と同じ場で比べたいので、システムが組み立てたコンテキストを借りる。
    //   dt=0 なら両実装とも進行度を進めないので、形だけを突き合わせられる。
    FormationContext ctx = MakePreviewContext();
    if (context.systemManager)
    {
        if (auto *system = context.systemManager->GetSystem<DroneFormationSystem>())
            ctx = system->MakeContext(0.0f);
    }

    // 実戦と同じ機数で測る。少なすぎると格子の端数の扱いの差が出ない。
    constexpr int   kSampleDrones = 100;
    constexpr int   kSteps        = 20;
    constexpr float kStepSize     = 1.0f / kSteps;

    float worstError    = 0.0f;
    int   worstIndex    = -1;
    float worstProgress = 0.0f;

    for (int step = 0; step <= kSteps; ++step)
    {
        const float progress = step * kStepSize;

        for (int i = 0; i < kSampleDrones; ++i)
        {
            DroneComponent a;
            a.localIndex     = static_cast<uint16_t>(i);
            a.totalDrones    = kSampleDrones;
            a.wantsAttack    = true;
            a.attackProgress = progress;
            a.currentState   = DroneState::Idle;

            DroneComponent b = a;

            native->Calculate(a, ctx);
            data->Calculate(b, ctx);

            const float error = (a.targetPosition - b.targetPosition).Length();
            if (error > worstError)
            {
                worstError    = error;
                worstIndex    = i;
                worstProgress = progress;
            }
        }
    }

    m_abMaxError      = worstError;
    m_abWorstIndex    = worstIndex;
    m_abWorstProgress = worstProgress;
}

// ============================================================================
// プレビュー
// ============================================================================
void FormationEditorWindow::EvaluatePoints(Data &formation, float progress)
{
    const FormationContext ctx = MakePreviewContext();

    m_points.clear();
    m_seats.clear();
    m_points.reserve(static_cast<size_t>(m_previewDrones));
    m_seats.reserve(static_cast<size_t>(m_previewDrones));

    for (int i = 0; i < m_previewDrones; ++i)
    {
        DroneComponent drone;
        drone.localIndex     = static_cast<uint16_t>(i);
        drone.totalDrones    = static_cast<uint16_t>(m_previewDrones);
        drone.wantsAttack    = true;
        drone.attackProgress = progress;
        drone.currentState   = DroneState::Idle;

        // 席は陣形本体と同じ関数で引く。ここで割付を書き直すと、
        // 片方だけ直したときに絵と実機がずれる。
        m_seats.push_back(formation.AssignSeat(drone));

        formation.Calculate(drone, ctx);
        m_points.push_back(drone.targetPosition);
    }
}

void FormationEditorWindow::DrawPreviewPane(Data &formation)
{
    const Spec &spec = formation.spec;

    ImGui::SetNextItemWidth(150.0f);
    ImGui::SliderInt("機数", &m_previewDrones, 4, 256);

    ImGui::SameLine();
    if (ImGui::Button(m_playing ? "停止" : "再生", ImVec2(60, 0)))
        m_playing = !m_playing;

    ImGui::SameLine();
    if (ImGui::Button("予備動作", ImVec2(80, 0)))
    {
        // 震え(Tell)は進行度0のときだけ出る。再生では一瞬で通り過ぎるので、
        // 「読ませたい形」を確認するための戻し口を用意する。
        m_playing         = false;
        m_previewProgress = 0.0f;
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##progress", &m_previewProgress, 0.0f, 1.0f, "進行度 %.2f");

    if (m_playing)
    {
        // 実機と同じ速さで進める（実機は毎フレーム dt × speed を足している）。
        m_previewProgress += ImGui::GetIO().DeltaTime * spec.timing.speed;
        if (m_previewProgress > 1.35f) m_previewProgress = 0.0f; // 振り切った形を少し見せてから戻す
    }
    const float shown = (m_previewProgress > 1.0f) ? 1.0f : m_previewProgress;

    // ---- 表示範囲：進行度を通した全体の広がりで決める ----
    //   今の進行度だけで合わせると、スクラブのたびに絵が拡大縮小して読めなくなる。
    m_boundsMin = Vector3(0.0f, 0.0f, 0.0f);
    m_boundsMax = Vector3(0.0f, 0.0f, kPreviewPlayerDistance);

    constexpr int kBoundsSamples = 8;
    for (int s = 0; s <= kBoundsSamples; ++s)
    {
        EvaluatePoints(formation, static_cast<float>(s) / kBoundsSamples);
        for (const Vector3 &p : m_points)
        {
            m_boundsMin = Vector3::Min(m_boundsMin, p);
            m_boundsMax = Vector3::Max(m_boundsMax, p);
        }
    }

    EvaluatePoints(formation, shown);

    DrawLegend();

    const float paneHeight = (ImGui::GetContentRegionAvail().y - ImGui::GetStyle().ItemSpacing.y * 3.0f) * 0.5f;

    DrawCanvas("##top", "上から見た図　横=右　縦=前", 0, 2, paneHeight);
    DrawCanvas("##side", "横から見た図　横=前　縦=上", 2, 1, paneHeight);
}

void FormationEditorWindow::DrawLegend()
{
    if (m_sideGroups <= 1 && m_tierGroups <= 1)
    {
        ImGui::TextDisabled("色 = 並び順（青→橙）。0 が先頭の機体");
        return;
    }

    ImGui::TextDisabled("色 =");
    for (int tier = 0; tier < m_tierGroups; ++tier)
    {
        for (int side = 0; side < m_sideGroups; ++side)
        {
            Data::Seat seat;
            seat.side = side;
            seat.tier = tier;

            ImGui::SameLine();
            ImGui::ColorButton("##swatch", ImColor(SeatColor(0, seat)).Value,
                               ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                               ImVec2(12, 12));
            ImGui::SameLine(0.0f, 4.0f);

            if (m_tierGroups > 1 && m_sideGroups > 1)
                ImGui::Text("%s%s", (tier == 0) ? "下" : "上", (side == 0) ? "A" : "B");
            else if (m_sideGroups > 1)
                ImGui::Text("%s", (side == 0) ? "A側" : "B側");
            else
                ImGui::Text("%d段目", tier + 1);
        }
    }
}

unsigned int FormationEditorWindow::SeatColor(size_t index, const Data::Seat &seat) const
{
    // グループが1つしか無いなら、色でグループを示しても情報が無い。
    // 代わりに並び順を見せる（割付で一番分からないのが「どの順に埋まるか」なので）。
    if (m_sideGroups <= 1 && m_tierGroups <= 1)
    {
        const float total = (m_points.size() > 1) ? static_cast<float>(m_points.size() - 1) : 1.0f;
        const float t     = static_cast<float>(index) / total;
        return IM_COL32(static_cast<int>(80 + 175 * t),
                        static_cast<int>(230 - 120 * t),
                        static_cast<int>(255 - 130 * t), 255);
    }

    const float hue   = (m_sideGroups <= 1) ? 0.55f
                                            : static_cast<float>(seat.side) / m_sideGroups * 0.75f;
    const float value = (m_tierGroups <= 1)
                          ? 1.0f
                          : 0.55f + 0.45f * (static_cast<float>(seat.tier) / (m_tierGroups - 1));

    float r = 0.0f, g = 0.0f, b = 0.0f;
    ImGui::ColorConvertHSVtoRGB(hue, 0.75f, value, r, g, b);
    return IM_COL32(static_cast<int>(r * 255), static_cast<int>(g * 255), static_cast<int>(b * 255), 255);
}

void FormationEditorWindow::DrawCanvas(const char *label, const char *caption, int axisH, int axisV, float height)
{
    ImGui::TextDisabled("%s", caption);

    const ImVec2 size(ImGui::GetContentRegionAvail().x, height - ImGui::GetTextLineHeightWithSpacing());
    if (size.x < 32.0f || size.y < 32.0f) return;

    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + size.x, p0.y + size.y);
    ImGui::InvisibleButton(label, size);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, IM_COL32(16, 16, 20, 255));
    dl->AddRect(p0, p1, IM_COL32(70, 70, 80, 255));
    dl->PushClipRect(p0, p1, true);

    // ---- ワールド → 画面。縦横で倍率を変えると形が歪むので共通にする ----
    const float minH  = AxisValue(m_boundsMin, axisH);
    const float maxH  = AxisValue(m_boundsMax, axisH);
    const float minV  = AxisValue(m_boundsMin, axisV);
    const float maxV  = AxisValue(m_boundsMax, axisV);
    const float spanH = (maxH - minH) < 1.0f ? 1.0f : (maxH - minH);
    const float spanV = (maxV - minV) < 1.0f ? 1.0f : (maxV - minV);

    constexpr float kPadding = 18.0f;
    const float     scale    = (std::min)((size.x - kPadding * 2.0f) / spanH,
                                          (size.y - kPadding * 2.0f) / spanV);

    const float  centerH = (minH + maxH) * 0.5f;
    const float  centerV = (minV + maxV) * 0.5f;
    const ImVec2 origin((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);

    auto toScreenH = [&](float world) { return origin.x + (world - centerH) * scale; };
    auto toScreenV = [&](float world) { return origin.y - (world - centerV) * scale; }; // 画面Yは下向き
    auto toScreen  = [&](const Vector3 &world) {
        return ImVec2(toScreenH(AxisValue(world, axisH)), toScreenV(AxisValue(world, axisV)));
    };

    // ---- 目盛り。何メートルの話をしているのかが分からないと数値を詰められない ----
    const float step = NiceStep((std::max)(spanH, spanV));

    for (float v = std::ceil(minH / step) * step; v <= maxH; v += step)
    {
        const float x = toScreenH(v);
        const bool  isOrigin = (std::fabs(v) < step * 0.01f);
        dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p1.y),
                    isOrigin ? IM_COL32(80, 80, 100, 255) : IM_COL32(40, 40, 48, 255));
    }
    for (float v = std::ceil(minV / step) * step; v <= maxV; v += step)
    {
        const float y = toScreenV(v);
        const bool  isOrigin = (std::fabs(v) < step * 0.01f);
        dl->AddLine(ImVec2(p0.x, y), ImVec2(p1.x, y),
                    isOrigin ? IM_COL32(80, 80, 100, 255) : IM_COL32(40, 40, 48, 255));
    }

    // ---- ボスとプレイヤー ----
    const ImVec2 bossPos   = toScreen(Vector3::Zero);
    const ImVec2 playerPos = toScreen(Vector3(0.0f, 0.0f, kPreviewPlayerDistance));

    dl->AddLine(bossPos, playerPos, IM_COL32(60, 70, 90, 255));
    dl->AddCircleFilled(bossPos, 5.0f, IM_COL32(230, 120, 60, 255));
    dl->AddText(ImVec2(bossPos.x + 7.0f, bossPos.y - 7.0f), IM_COL32(230, 120, 60, 255), "BOSS");
    dl->AddCircleFilled(playerPos, 5.0f, IM_COL32(90, 200, 255, 255));
    dl->AddText(ImVec2(playerPos.x + 7.0f, playerPos.y - 7.0f), IM_COL32(90, 200, 255, 255), "PLAYER");

    // ---- ドローン ----
    for (size_t i = 0; i < m_points.size(); ++i)
    {
        const Data::Seat &seat = (i < m_seats.size()) ? m_seats[i] : Data::Seat{};
        dl->AddCircleFilled(toScreen(m_points[i]), 2.5f, SeatColor(i, seat));
    }

    // 先頭の1機だけ番号を出す。割付の起点がどこかは形を読むときの手掛かりになる。
    if (!m_points.empty())
        dl->AddText(toScreen(m_points[0]), IM_COL32(255, 255, 255, 220), "0");

    // ---- 目盛りの単位 ----
    char scaleText[32];
    std::snprintf(scaleText, sizeof(scaleText), "格子 %.4g m", step);
    dl->AddText(ImVec2(p0.x + 6.0f, p1.y - ImGui::GetTextLineHeight() - 4.0f),
                IM_COL32(150, 150, 160, 255), scaleText);

    dl->PopClipRect();
}

// ============================================================================
// パラメータ編集
// ============================================================================
void FormationEditorWindow::DrawAssignSummary(const Spec &spec)
{
    const int sides    = (std::max)(1, spec.assign.sideGroups);
    const int tiers    = (std::max)(1, spec.assign.tierGroups);
    const int groups   = sides * tiers;
    const int perGroup = (groups > 1) ? (std::max)(1, m_previewDrones / groups) : m_previewDrones;

    char buffer[192];
    if (spec.assign.rows <= 0)
    {
        const int cols = (std::max)(1, static_cast<int>(std::floor(std::sqrt(static_cast<float>(perGroup)))));
        std::snprintf(buffer, sizeof(buffer), "%d機 → %dグループ × %d機 → 自動格子 %d列 × 約%d行",
                      m_previewDrones, groups, perGroup, cols, (perGroup + cols - 1) / cols);
    }
    else
    {
        const int rows      = spec.assign.rows;
        const int fullCols  = (std::max)(1, perGroup / rows);
        const int remainder = perGroup % rows;
        std::snprintf(buffer, sizeof(buffer), "%d機 → %dグループ × %d機 → %d列 × %d行%s",
                      m_previewDrones, groups, perGroup, fullCols, rows,
                      (remainder != 0) ? "（＋端数）" : "");
    }

    ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.6f, 1.0f), "%s", buffer);
}

void FormationEditorWindow::DrawAssignSection(Spec &spec)
{
    if (!ImGui::TreeNodeEx("① 割付　誰がどこに座るか", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::SliderInt("左右に分ける数", &spec.assign.sideGroups, 1, 4);
    Hint("2 にすると左右2組に分かれ、③の変換が左右で逆向きにかかる。\n"
         "挟み込みの「左右の腕」がこれ。1 なら分けない。");

    ImGui::SliderInt("上下に分ける数", &spec.assign.tierGroups, 1, 4);
    Hint("2 にすると上下2段に分かれる。④の時間差の単位を「段ごと」にすると、\n"
         "下段が先／上段が先といった先攻後攻が作れる。");

    int split = static_cast<int>(spec.assign.split);
    if (ImGui::Combo("配り方", &split, "1機ずつ交互\0連続したかたまり\0"))
        spec.assign.split = static_cast<SpecSplit>(split);
    Hint("グループへの配り方。\n"
         "交互 … 0,1,0,1… と配る。撃破されても左右の数が偏りにくい。\n"
         "かたまり … 前半を1グループ、後半を別グループにする。");

    ImGui::SliderInt("1列に積む数", &spec.assign.rows, 0, 8);
    Hint("1つの列に縦へ何機積むか。0 にすると総数から正方形に近い格子を\n"
         "自動で作る（縦割りの刃がこれ）。");

    DrawAssignSummary(spec);
    ImGui::TreePop();
}

void FormationEditorWindow::DrawShapeSection(Spec &spec)
{
    if (!ImGui::TreeNodeEx("② 形　席をどこに置くか", ImGuiTreeNodeFlags_DefaultOpen)) return;

    int shapeType = static_cast<int>(spec.shape.type);
    if (ImGui::Combo("種類", &shapeType, "格子（刃・壁）\0腕（放射状）\0"))
        spec.shape.type = static_cast<SpecShape>(shapeType);
    Hint("格子 … まっすぐ並べる。刃や壁になる。\n"
         "腕　 … ボスから放射状に伸ばす。薙ぎ払いや挟み込みの腕になる。");

    const bool isArm = (spec.shape.type == SpecShape::Arm);

    if (!isArm)
    {
        int columnAxis = static_cast<int>(spec.shape.columnAxis);
        if (ImGui::Combo("列を並べる軸", &columnAxis, "右（横に広げる）\0上\0前（奥行きに伸ばす）\0"))
            spec.shape.columnAxis = static_cast<SpecAxis>(columnAxis);
    }

    ImGui::SliderFloat(isArm ? "半径方向の間隔" : "列の間隔", &spec.shape.spacingA, 0.2f, 8.0f, "%.2f m");
    ImGui::SliderFloat("縦（行）の間隔", &spec.shape.spacingB, 0.2f, 8.0f, "%.2f m");

    ImGui::Checkbox("列を中央に揃える", &spec.shape.centerColumns);
    Hint("ON … 形の中心がボスの正面に来る（刃・壁）。\n"
         "OFF … 下の起点から片側へ伸びる（腕）。");

    if (!spec.shape.centerColumns)
        ImGui::SliderFloat(isArm ? "最も内側の半径" : "列の起点", &spec.shape.baseA, 0.0f, 20.0f, "%.2f m");

    ImGui::SliderFloat("下端の高さ", &spec.shape.baseB, -5.0f, 20.0f, "%.2f m");
    Hint("形の一番下の段の高さ。ヒンジ回転ではここが回転軸になる。");

    if (spec.assign.tierGroups > 1)
    {
        ImGui::SliderFloat("段の高さ差", &spec.shape.tierStepB, 0.0f, 10.0f, "%.2f m");
        Hint("上下に分けたとき、1段上がるごとに足す高さ。\n"
             "プレイヤーのジャンプ高さと合わせて決める。");
    }

    ImGui::Spacing();
    ImGui::TextDisabled("ボスからの原点オフセット");
    ImGui::SliderFloat("前", &spec.shape.originForward, -20.0f, 20.0f, "%.2f m");
    ImGui::SliderFloat("上", &spec.shape.originUp, -10.0f, 20.0f, "%.2f m");
    ImGui::SliderFloat("右", &spec.shape.originRight, -20.0f, 20.0f, "%.2f m");
    ImGui::TreePop();
}

void FormationEditorWindow::DrawTransformSection(Spec &spec)
{
    if (!ImGui::TreeNodeEx("③ 変換　形をどう動かすか", ImGuiTreeNodeFlags_DefaultOpen)) return;

    int transformType = static_cast<int>(spec.transform.type);
    if (ImGui::Combo("種類", &transformType,
                     "なし（静止）\0ヒンジ回転（倒す）\0ヨー回転（振る）\0平行移動（寄せる）\0"))
        spec.transform.type = static_cast<SpecTransform>(transformType);
    Hint("ヒンジ回転 … 下端を軸に前へ倒す。縦割り。\n"
         "ヨー回転　 … 腕の向きを横に振る。薙ぎ払い・挟み込み。\n"
         "平行移動　 … 形ごとスライドさせる。壁が左右から寄る。");

    if (spec.transform.type == SpecTransform::None)
    {
        ImGui::TextDisabled("静止した形（予備動作だけの陣形）");
        ImGui::TreePop();
        return;
    }

    if (spec.transform.type == SpecTransform::Translate)
    {
        int moveAxis = static_cast<int>(spec.transform.axis);
        if (ImGui::Combo("移動軸", &moveAxis, "右（左右から寄る）\0上（上下に動く）\0前（前へ押し出す）\0"))
            spec.transform.axis = static_cast<SpecAxis>(moveAxis);
    }

    const bool  isAngle = (spec.transform.type == SpecTransform::Hinge
                          || spec.transform.type == SpecTransform::YawSweep);
    const float range   = isAngle ? 180.0f : 30.0f;
    const char *format  = isAngle ? "%.1f 度" : "%.2f m";

    ImGui::SliderFloat("開始値（予備動作）", &spec.transform.from, -range, range, format);
    ImGui::SliderFloat("終了値（振り切り）", &spec.transform.to, -range, range, format);

    // 今スクラブしている瞬間の値。スライダの数字と画面の形が結び付かないのが
    // このブロックの分かりにくさなので、間の値を出しておく。
    {
        const float p      = (m_previewProgress > 1.0f) ? 1.0f : m_previewProgress;
        const float amount = spec.timing.curve.empty() ? (p * p) : spec.timing.curve.Evaluate(p);
        const float value  = spec.transform.from + (spec.transform.to - spec.transform.from) * amount;
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.6f, 1.0f),
                           isAngle ? "今の値 %.1f 度（進行度 %.2f）" : "今の値 %.2f m（進行度 %.2f）",
                           value, p);
    }

    ImGui::Checkbox("左右で向きを反転", &spec.transform.mirrorBySide);
    Hint("①で左右に分けたとき、変換の向きを左右で逆にする。\n"
         "これが無いと両腕が同じ方向へ動いてしまう。");

    if (spec.transform.mirrorBySide)
        ImGui::Checkbox("反転の向きを入れ替える", &spec.transform.mirrorFlip);

    ImGui::TreePop();
}

void FormationEditorWindow::DrawTimingSection(Spec &spec)
{
    if (!ImGui::TreeNodeEx("④ 時間　いつ動くか", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::SliderFloat("速さ", &spec.timing.speed, 0.1f, 8.0f, "%.2f /秒");
    ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.6f, 1.0f), "振り切るまで %.2f 秒",
                       (spec.timing.speed > 0.0f) ? 1.0f / spec.timing.speed : 0.0f);

    int phaseSource = static_cast<int>(spec.timing.phaseSource);
    if (ImGui::Combo("時間差", &phaseSource, "なし（全機いっせいに）\0列ごと（波が走る）\0段ごと（先攻後攻）\0"))
        spec.timing.phaseSource = static_cast<SpecPhaseSource>(phaseSource);
    Hint("列ごと … 端から順に動き出す。波状の縦割りがこれ。\n"
         "段ごと … ①で上下に分けた段の間で先攻後攻を作る。");

    if (spec.timing.phaseSource != SpecPhaseSource::None)
    {
        ImGui::SliderFloat("1つが動く時間の幅", &spec.timing.phaseOverlap, 0.05f, 1.0f, "%.2f");
        Hint("全体の進行度のうち、1つの列（段）が動き切るのに使う割合。\n"
             "小さいほど波が細く、鋭くなる。1.0 で時間差なしと同じ。");

        ImGui::Checkbox("順序を反転", &spec.timing.phaseReverse);
        Hint("波の向きを逆にする。左→右 と 右→左 の作り分けがこれ。");
    }

    ImGui::TextDisabled("カーブはウィンドウ下部で編集");
    ImGui::TreePop();
}

void FormationEditorWindow::DrawTellSection(Spec &spec)
{
    if (!ImGui::TreeNodeEx("⑤ 予備動作の見せ　どこから来るか読ませる")) return;

    ImGui::Checkbox("震えで先攻を読ませる", &spec.tell.enabled);
    Hint("先に動く側だけを小刻みに揺らして、プレイヤーに回避方向を読ませる。\n"
         "進行度0（＝攻撃前）のときだけ出る。上の「予備動作」ボタンで確認できる。");

    if (spec.tell.enabled)
    {
        int tellAxis = static_cast<int>(spec.tell.axis);
        if (ImGui::Combo("震える軸", &tellAxis, "右（左右に）\0上（上下に）\0前（前後に）\0"))
            spec.tell.axis = static_cast<SpecAxis>(tellAxis);

        ImGui::SliderFloat("震え幅", &spec.tell.amplitude, 0.0f, 3.0f, "%.2f m");
        ImGui::SliderFloat("震えの速さ", &spec.tell.frequency, 1.0f, 30.0f, "%.1f");
        ImGui::SliderFloat("先攻とみなす範囲", &spec.tell.leadThreshold, 0.0f, 1.0f, "%.2f");
        Hint("これ以下の順番の列（段）だけが震える。\n"
             "0.18 くらいにすると「最初に来る一群」だけが光る。");

        if (spec.timing.phaseSource == SpecPhaseSource::None)
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "④の時間差が「なし」だと全機が震えます");
    }
    ImGui::TreePop();
}

void FormationEditorWindow::DrawOutputSection(Spec &spec)
{
    if (!ImGui::TreeNodeEx("⑥ 出力　向きと当たり方")) return;

    int look = static_cast<int>(spec.look);
    if (ImGui::Combo("機首の向き", &look, "自動（形なり）\0プレイヤーへ\0"))
        spec.look = static_cast<SpecLook>(look);
    Hint("自動 … 倒れる接線・腕の向きなど、形から決まる向きを使う。\n"
         "プレイヤーへ … 腕は横を向いたまま機首だけ相手を向く（挟み込み）。");

    int chargeMode = static_cast<int>(spec.chargeMode);
    if (ImGui::Combo("突撃中の移動", &chargeMode, "直進（撃ち出す）\0剛体追従（形を保つ）\0バネ追従（しなる）\0"))
        spec.chargeMode = static_cast<ChargeMode>(chargeMode);
    Hint("形が動く技は「剛体追従」。形が歪まずスライドする。\n"
         "「直進」は各機が独立に飛ぶ弾幕向けで、この形式では基本使わない。");

    ImGui::Checkbox("地面へのめり込みを止める", &spec.clampToGround);
    if (spec.clampToGround)
        ImGui::SliderFloat("最低高さ", &spec.groundClearance, 0.0f, 3.0f, "%.2f m");

    ImGui::TreePop();
}
