// 陣形(FormationSpec)の編集UI。
#include "FormationEditorWindow.h"

#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h" // BehaviorTreeComponent
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Game/Logics/AI/Drone/DroneFormationSystem.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"

#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"

#include <algorithm>
#include <cstdio>
#include <imgui.h>
#include <json.hpp>

using namespace CCL::AI::Formation;

namespace
{
// ============================================================================
// 動きの「型」
//
// 値は Assets/Formations/*.json の実測値をそのまま持ってきている。
// つまり「C++実装との照合が済んでいる組み合わせ」だけを型にしている。
// 適当に作った初期値ではないので、押した直後から実機で通用する形が出る。
//
// name と curve は上書きしない:
//   name はアセットの識別子なので、型を変えても変わってはいけない。
//   curve は「タメてから一気に」のような、型とは独立した味付けなので残す。
// ============================================================================
struct MotionPreset
{
    const char *name;
    const char *desc;
};

constexpr int kPresetCount = 6;

const MotionPreset kPresets[kPresetCount] = {
    {"振り下ろす", "ボスの上に刃を立てて、前へ倒す（縦割り）"},
    {"挟み込む", "左右2本の腕が、正面で閉じる"},
    {"薙ぎ払う", "片側へ伸ばした腕を、横へ振り抜く"},
    {"波で振り下ろす", "横一列を、端から順に倒していく"},
    {"上下2段で挟む", "上下4枚の壁が、時間差で左右から寄る"},
    {"静止（見せるだけ）", "動かさない。形だけ作って出す"},
};

FormationSpec MakePresetSpec(int index)
{
    FormationSpec s; // 既定値から始めて、型ごとに要る所だけ書き換える

    switch (index)
    {
    case 0: // 振り下ろす ← GuillotineSlam.json
        s.assign              = {1, 1, SpecSplit::Block, 0};
        s.shape.type          = SpecShape::Grid;
        s.shape.columnAxis    = SpecAxis::Right;
        s.shape.spacingA      = 2.0f;
        s.shape.spacingB      = 2.0f;
        s.shape.centerColumns = true;
        s.shape.originUp      = 2.0f;
        s.transform.type      = SpecTransform::Hinge;
        s.transform.from      = 0.0f;
        s.transform.to        = 108.86f;
        s.timing.speed        = 2.5f;
        s.look                = SpecLook::Auto;
        s.clampToGround       = true;
        break;

    case 1: // 挟み込む ← ClapSlam.json
        s.assign                 = {2, 1, SpecSplit::Interleaved, 2};
        s.shape.type             = SpecShape::Arm;
        s.shape.spacingA         = 1.2f;
        s.shape.spacingB         = 1.5f;
        s.shape.centerColumns    = false;
        s.shape.baseA            = 3.0f;
        s.shape.baseB            = 0.8f;
        s.transform.type         = SpecTransform::YawSweep;
        s.transform.from         = 85.94f;
        s.transform.to           = 4.58f;
        s.transform.mirrorBySide = true;
        s.timing.speed           = 3.0f;
        s.look                   = SpecLook::ToPlayer; // 腕は横向きのまま機首だけ相手を向く
        break;

    case 2: // 薙ぎ払う ← SideSlapWhip.json
        s.assign              = {1, 1, SpecSplit::Block, 3};
        s.shape.type          = SpecShape::Arm;
        s.shape.spacingA      = 2.0f;
        s.shape.spacingB      = 1.5f;
        s.shape.centerColumns = false;
        s.shape.baseA         = 4.0f;
        s.shape.baseB         = 1.5f;
        s.transform.type      = SpecTransform::YawSweep;
        s.transform.from      = 100.27f;
        s.transform.to        = -100.27f;
        s.timing.speed        = 1.6f;
        break;

    case 3: // 波で振り下ろす ← TsunamiWave.json
        s.assign              = {1, 1, SpecSplit::Block, 3};
        s.shape.type          = SpecShape::Grid;
        s.shape.columnAxis    = SpecAxis::Right;
        s.shape.spacingA      = 2.5f;
        s.shape.spacingB      = 2.0f;
        s.shape.centerColumns = true;
        s.shape.originForward = 5.0f;
        s.shape.originUp      = 2.0f;
        s.transform.type      = SpecTransform::Hinge;
        s.transform.from      = 0.0f;
        s.transform.to        = 108.86f;
        s.timing.speed        = 1.0f;
        s.timing.phaseSource  = SpecPhaseSource::Column;
        s.timing.phaseOverlap = 0.35f;
        s.tell.enabled        = true;
        s.tell.axis           = SpecAxis::Up;
        s.tell.amplitude      = 0.5f;
        s.tell.frequency      = 12.0f;
        s.clampToGround       = true;
        break;

    case 4: // 上下2段で挟む ← MegaClapSlam_LowerFirst.json
        s.assign                 = {2, 2, SpecSplit::Block, 2};
        s.shape.type             = SpecShape::Grid;
        s.shape.columnAxis       = SpecAxis::Forward;
        s.shape.spacingA         = 2.5f;
        s.shape.spacingB         = 1.2f;
        s.shape.centerColumns    = true;
        s.shape.baseB            = 0.4f;
        s.shape.tierStepB        = 2.4f;
        s.shape.originForward    = 6.0f;
        s.transform.type         = SpecTransform::Translate;
        s.transform.axis         = SpecAxis::Right;
        s.transform.from         = 12.0f;
        s.transform.to           = 1.2f;
        s.transform.mirrorBySide = true;
        s.transform.mirrorFlip   = true;
        s.timing.speed           = 1.2f;
        s.timing.phaseSource     = SpecPhaseSource::Tier;
        s.timing.phaseOverlap    = 0.5f;
        s.tell.enabled           = true;
        s.tell.amplitude         = 0.6f;
        break;

    default: // 静止
        s.shape.spacingA = 2.0f;
        s.shape.spacingB = 2.0f;
        s.shape.originUp = 2.0f;
        s.transform.type = SpecTransform::None;
        s.timing.speed   = 2.5f;
        break;
    }
    return s;
}

// 今の spec がどの型かを言い当てる。
//   骨格（形・変換・時間差・グループ数）だけで見る。間隔や角度をいじっても
//   「振り下ろす を調整中」であることは変わらないので、そこは判定に入れない。
int MatchPreset(const FormationSpec &s)
{
    for (int i = 0; i < kPresetCount; ++i)
    {
        const FormationSpec p = MakePresetSpec(i);
        if (p.shape.type == s.shape.type && p.transform.type == s.transform.type
            && p.timing.phaseSource == s.timing.phaseSource
            && p.assign.sideGroups == s.assign.sideGroups
            && p.assign.tierGroups == s.assign.tierGroups)
            return i;
    }
    return -1;
}
} // namespace

// ============================================================================
// 補助
// ============================================================================
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

bool FormationEditorWindow::FindBoss(EditorContext &context, BehaviorTreeComponent **bt,
                                     BossCommandComponent **cmd)
{
    *bt  = nullptr;
    *cmd = nullptr;
    if (!context.world) return false;

    // 1v1 前提なので最初の1体だけ見る（DroneCommandSystem と同じ扱い）。
    for (auto boss : context.world->View<BossCommandComponent, BehaviorTreeComponent>())
    {
        *bt  = context.world->GetComponent<BehaviorTreeComponent>(boss);
        *cmd = context.world->GetComponent<BossCommandComponent>(boss);
        break;
    }
    return (*bt != nullptr && *cmd != nullptr);
}

void FormationEditorWindow::ReleaseStage(EditorContext &context)
{
    m_staged  = false;
    m_playing = false;

    if (context.systemManager)
    {
        if (auto *system = context.systemManager->GetSystem<DroneFormationSystem>())
            system->SetDebugProgress(-1.0f); // 進行度を陣形側へ返す
    }

    BehaviorTreeComponent *bt  = nullptr;
    BossCommandComponent  *cmd = nullptr;
    if (FindBoss(context, &bt, &cmd))
    {
        bt->debugPaused        = false;
        cmd->droneAttackActive = false;
        cmd->requestFormation  = DroneFormationType::OrbitCircle; // 待機の周回へ戻す
    }
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

    ImGui::SetNextItemWidth(240.0f);
    if (ImGui::Combo("陣形", &m_editTarget, names.data(), count))
    {
        m_abDone         = false;
        m_abMaxError     = -1.0f;
        m_baselineTarget = -1; // 対象が変わったので控えを取り直す

        // 別の陣形へ移ったら、実機に出しているのは古い方なので降ろす。
        if (m_staged) ReleaseStage(context);
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

    ImGui::Separator();

    DrawStageControls(context, type);

    ImGui::Separator();

    DrawPresetGallery(spec);
    DrawParameters(spec);

    ImGui::Spacing();
    DrawSpecSummary(spec);

    // ---- カーブ ----
    //   既存の C++ 陣形は eased = p*p 固定で、「タメてから一気に」等が書けなかった。
    //   ここが唯一その表現を作れる場所になる。
    ImGui::SeparatorText("進行度 → 動く量");
    if (spec.timing.curve.empty())
    {
        ImGui::TextDisabled("カーブ未設定（p×p の easeIn で動作中）");
        Hint("既存の C++ 陣形はすべて p×p 固定だった。\n"
             "カーブを作ると「最初は動かず後半で一気に」「途中で止めて見せる」\n"
             "といった、コードでは書けなかった動きが作れる。");
        if (ImGui::Button("カーブを作って編集する", ImVec2(220, 24)))
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
        options.height       = 150.0f;
        options.showPlayhead = true;
        options.playheadX    = m_progress;

        m_curveWidget.Draw("##formationCurve", curves, options);

        if (ImGui::Button("カーブを消す (p×p へ戻す)", ImVec2(220, 24)))
            spec.timing.curve.keys.clear();
    }

    // ---- 保存 ----
    ImGui::SeparatorText("保存");
    const std::string path = formation->assetPath.empty()
                               ? DroneFormationSystem::SpecAssetPath(assets[m_editTarget].name)
                               : formation->assetPath;
    DrawAssetControls(context, *formation, path);
}

// ============================================================================
// 実機に出す
// ============================================================================
void FormationEditorWindow::DrawStageControls(EditorContext &context, DroneFormationType type)
{
    auto &registry = FormationRegistry::Instance();

    // ---- 編集が実機へ届く経路が繋がっているか ----
    //   これが OFF だと、何をどう編集しても実機は C++ 実装のまま動く。
    //   このUIで一番きつい落とし穴なので先頭に置く。
    {
        bool prefer = registry.PreferData();
        if (ImGui::Checkbox("このアセットを実機で使う", &prefer)) registry.SetPreferData(prefer);
        Hint("OFF の間、実機のドローンは C++ 実装で動く。\n"
             "ここを ON にしないと、編集した値はどこにも反映されない。\n"
             "起動のたびに OFF へ戻る。");

        if (!prefer)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "← 入れるまで反映されません");
        }
    }

    BehaviorTreeComponent *bt  = nullptr;
    BossCommandComponent  *cmd = nullptr;
    if (!FindBoss(context, &bt, &cmd))
    {
        m_staged = false;
        ImGui::TextDisabled("ボスが居ないシーンです。形を見るにはボスの居るシーンで開いてください。");
        return;
    }

    // ---- 掌握 / 返却 ----
    if (!m_staged)
    {
        if (ImGui::Button("実機に出す", ImVec2(130, 28)))
        {
            // 見るのに要る4つを同時に立てる。1つでも欠けると何も見えないので、
            // 手順に分けず1操作にする。
            bt->debugPaused        = true;  // BT が毎フレーム別の技を選ぶのを止める
            cmd->currentActionId   = 0;     // 握ったままの行動要求を下ろす
            cmd->requestFormation  = type;  // この陣形を要求する
            cmd->droneAttackActive = true;  // 攻撃状態（形が動く技はこれで wantsAttack が立つ）
            m_staged               = true;
        }
        Hint("ボスAIを止めて、この陣形を出し、進行度を手送りに切り替える。\n"
             "ボスの歩行も止まるので、陣形の原点が動かなくなる。");
        return;
    }

    if (ImGui::Button("ゲームへ戻す", ImVec2(130, 28))) ReleaseStage(context);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.6f, 1.0f), "実機に出しています");

    // 掌握中は毎フレーム押さえ直す。
    //   他のデバッグ操作やシーン遷移で剥がれても、次のフレームで戻る。
    bt->debugPaused        = true;
    cmd->requestFormation  = type;
    cmd->droneAttackActive = true;

    // ---- 進行度の手送り ----
    if (m_playing)
    {
        // 実機と同じ速さで進める。振り切った形を少し見せてから頭へ戻す。
        auto *data = registry.GetData(type);
        const float speed = data ? data->spec.timing.speed : 1.0f;

        m_progress += ImGui::GetIO().DeltaTime * speed;
        if (m_progress > 1.35f) m_progress = 0.0f;
    }

    const float shown = (m_progress > 1.0f) ? 1.0f : m_progress;

    if (ImGui::Button(m_playing ? "停止" : "再生", ImVec2(60, 0))) m_playing = !m_playing;
    ImGui::SameLine();
    if (ImGui::Button("先頭へ", ImVec2(60, 0)))
    {
        // 震え(Tell)は進行度0のときだけ出る。再生では一瞬で通り過ぎるので戻し口を置く。
        m_playing  = false;
        m_progress = 0.0f;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##progress", &m_progress, 0.0f, 1.0f, "進行度 %.2f")) m_playing = false;

    // 進行度をシステムへ渡す。以後、陣形は dt=0 で回るので勝手に進まない。
    if (context.systemManager)
    {
        if (auto *system = context.systemManager->GetSystem<DroneFormationSystem>())
            system->SetDebugProgress(shown);
    }
}

// ============================================================================
// 型
// ============================================================================
void FormationEditorWindow::DrawPresetGallery(Spec &spec)
{
    const int current = MatchPreset(spec);

    ImGui::SeparatorText("型");

    // 3列。ボタン名だけ並べ、説明はホバーに逃がす。
    if (ImGui::BeginTable("##presets", 3, ImGuiTableFlags_SizingStretchSame))
    {
        for (int i = 0; i < kPresetCount; ++i)
        {
            ImGui::TableNextColumn();

            // 今の型を1つだけ光らせる。「今どれを見ているのか」が
            // どこにも書かれていないのが、このUIで最初に迷う所だった。
            const bool active = (i == current);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.50f, 0.35f, 1.0f));

            if (ImGui::Button(kPresets[i].name, ImVec2(-1.0f, 28.0f)))
            {
                const FormationSpec preset = MakePresetSpec(i);

                // 識別子と味付けは持ち越す（MakePresetSpec の説明を参照）。
                const std::string saved      = spec.name;
                const CurveData   savedCurve = spec.timing.curve;
                spec                         = preset;
                spec.name                    = saved;
                spec.timing.curve            = savedCurve;
            }

            if (active) ImGui::PopStyleColor();

            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(kPresets[i].desc);
                ImGui::Separator();
                ImGui::TextDisabled("押すと下の値がこの型で上書きされます（戻すのは「変更を破棄」）");
                ImGui::EndTooltip();
            }
        }
        ImGui::EndTable();
    }

    if (current < 0)
        ImGui::TextDisabled("どの型にも当てはまらない設定です（JSON を手で書いた形）");
}

// ============================================================================
// 調整
// ============================================================================
void FormationEditorWindow::DrawParameters(Spec &spec)
{
    const bool isArm   = (spec.shape.type == SpecShape::Arm);
    const bool isAngle = (spec.transform.type == SpecTransform::Hinge
                          || spec.transform.type == SpecTransform::YawSweep);

    ImGui::SeparatorText("調整");

    ImGui::SliderFloat(isArm ? "腕の伸びる間隔" : "横の間隔", &spec.shape.spacingA, 0.2f, 8.0f, "%.2f m");
    ImGui::SliderFloat("縦の間隔", &spec.shape.spacingB, 0.2f, 8.0f, "%.2f m");
    Hint("機体どうしの間隔。広げると同じ機数でも形が大きくなり、\n"
         "隙間が空くのでプレイヤーが抜けられるようになる。");

    // spec は speed [1/秒] を持つが、それは「何秒で振り切るか」の逆数でしかない。
    //   2.5 と言われても速いのか遅いのか分からないので、秒のまま入れさせる。
    {
        float seconds = (spec.timing.speed > 0.01f) ? (1.0f / spec.timing.speed) : 1.0f;
        if (ImGui::SliderFloat("振り切るまでの時間", &seconds, 0.10f, 3.0f, "%.2f 秒"))
            spec.timing.speed = 1.0f / ((seconds < 0.01f) ? 0.01f : seconds);
        Hint("予備動作の終わりから、振り切るまでの時間。\n"
             "短いほど避けにくい。0.3秒を切ると見てから反応するのはほぼ無理。");
    }

    if (spec.transform.type == SpecTransform::None)
    {
        ImGui::TextDisabled("この型は動きません（形を見せるだけ）");
    }
    else
    {
        const float range  = isAngle ? 180.0f : 30.0f;
        const char *format = isAngle ? "%.1f 度" : "%.2f m";

        ImGui::SliderFloat("動き出し", &spec.transform.from, -range, range, format);
        ImGui::SliderFloat("振り切り", &spec.transform.to, -range, range, format);

        // 今の進行度での実際の値。スライダの数字と画面の形が結び付かないのが
        // このブロックの分かりにくさなので、間の値を出しておく。
        const float amount = spec.timing.curve.empty() ? (m_progress * m_progress)
                                                       : spec.timing.curve.Evaluate(m_progress);
        const float value  = spec.transform.from + (spec.transform.to - spec.transform.from) * amount;
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.6f, 1.0f),
                           isAngle ? "今 %.1f 度（進行度 %.2f）" : "今 %.2f m（進行度 %.2f）",
                           value, m_progress);
    }

    if (spec.timing.phaseSource != SpecPhaseSource::None)
    {
        // spec は phaseOverlap（1つが動き切るのに使う進行度の幅）を持つ。
        //   小さいほど鋭い＝値と印象が逆で、触っていて方向が分からない。
        //   ここでは 1 から引いて「大きいほど強い」向きに直して出す。
        float stagger = 1.0f - spec.timing.phaseOverlap;
        if (ImGui::SliderFloat("ずれの強さ", &stagger, 0.0f, 0.95f, "%.2f"))
            spec.timing.phaseOverlap = 1.0f - stagger;
        Hint(spec.timing.phaseSource == SpecPhaseSource::Column
                 ? "大きいほど、端から順に遅れて動く（波が細く鋭くなる）。\n0 で全列いっせいに動く。"
                 : "大きいほど、段の間の先攻後攻がはっきりする。\n0 で上下いっせいに動く。");

        ImGui::Checkbox(spec.timing.phaseSource == SpecPhaseSource::Column ? "波の向きを逆にする"
                                                                          : "上の段から先に動かす",
                        &spec.timing.phaseReverse);
    }

    // enabled と amplitude を1本にまとめる。振れ幅0で有効にする意味は無いので、
    // チェックボックスと数値の2段構えにする理由も無い。
    {
        float amp = spec.tell.enabled ? spec.tell.amplitude : 0.0f;
        if (ImGui::SliderFloat("予告の震え", &amp, 0.0f, 2.0f, "%.2f m"))
        {
            spec.tell.amplitude = amp;
            spec.tell.enabled   = (amp > 0.001f);
        }
        Hint("先に動く一群だけを小刻みに揺らして、回避方向を読ませる。\n"
             "0 で予告なし。攻撃前（進行度0）にだけ出るので「先頭へ」で確認する。");

        if (spec.tell.enabled && spec.timing.phaseSource == SpecPhaseSource::None)
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "この型は時間差が無いので、全機が震えます");
    }

    ImGui::SliderFloat("形の位置 前", &spec.shape.originForward, -20.0f, 20.0f, "%.2f m");
    ImGui::SliderFloat("形の位置 上", &spec.shape.originUp, -10.0f, 20.0f, "%.2f m");
    Hint("ボスから見た、形全体の置き場所。\n"
         "プレイヤーとの間合いはここで決まる。");
}

// ============================================================================
// 設定を文章にして返す
// ============================================================================
void FormationEditorWindow::DrawSpecSummary(const Spec &spec)
{
    const int sides = (std::max)(1, spec.assign.sideGroups);
    const int tiers = (std::max)(1, spec.assign.tierGroups);

    std::string s;
    char        buf[192];

    // 何組に分けるか
    if (sides > 1 || tiers > 1)
    {
        if (sides > 1)
        {
            std::snprintf(buf, sizeof(buf), "左右%d組", sides);
            s += buf;
        }
        if (sides > 1 && tiers > 1) s += "・";
        if (tiers > 1)
        {
            std::snprintf(buf, sizeof(buf), "上下%d段", tiers);
            s += buf;
        }
        s += "に分け、";
    }

    // どんな形に並べるか
    if (spec.shape.type == SpecShape::Arm)
    {
        std::snprintf(buf, sizeof(buf), "ボスから放射状の腕（間隔 %.1fm）に並べ、", spec.shape.spacingA);
    }
    else
    {
        const char *axis = (spec.shape.columnAxis == SpecAxis::Right) ? "横"
                         : (spec.shape.columnAxis == SpecAxis::Up)    ? "縦"
                                                                      : "奥行き";
        std::snprintf(buf, sizeof(buf), "%s方向の格子（間隔 %.1fm）に並べ、", axis, spec.shape.spacingA);
    }
    s += buf;

    // どう動くか
    const float seconds = (spec.timing.speed > 0.0f) ? 1.0f / spec.timing.speed : 0.0f;
    switch (spec.transform.type)
    {
    case SpecTransform::None:
        s += "動かさない（形を見せるだけ）。";
        break;
    case SpecTransform::Hinge:
        std::snprintf(buf, sizeof(buf), "%.2f秒かけて下端を軸に %.0f°→%.0f° 倒す。", seconds,
                      spec.transform.from, spec.transform.to);
        s += buf;
        break;
    case SpecTransform::YawSweep:
        std::snprintf(buf, sizeof(buf), "%.2f秒かけて %.0f°→%.0f° 横に振る。", seconds,
                      spec.transform.from, spec.transform.to);
        s += buf;
        break;
    case SpecTransform::Translate:
        std::snprintf(buf, sizeof(buf), "%.2f秒かけて %.1fm→%.1fm 平行移動する。", seconds,
                      spec.transform.from, spec.transform.to);
        s += buf;
        break;
    }

    if (spec.transform.mirrorBySide && sides > 1) s += "左右は逆向き。";

    if (spec.timing.phaseSource == SpecPhaseSource::Column)
    {
        std::snprintf(buf, sizeof(buf), " 列ごとに時間差（%s）。", spec.timing.phaseReverse ? "逆順" : "順");
        s += buf;
    }
    else if (spec.timing.phaseSource == SpecPhaseSource::Tier)
    {
        std::snprintf(buf, sizeof(buf), " 段ごとに先攻後攻（%s）。",
                      spec.timing.phaseReverse ? "上が先" : "下が先");
        s += buf;
    }

    if (spec.tell.enabled) s += " 先に来る側が震えて予告する。";

    ImGui::TextColored(ImVec4(0.6f, 0.85f, 0.65f, 1.0f), "この設定は:");
    ImGui::SameLine();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(s.c_str());
    ImGui::PopTextWrapPos();
}

// ============================================================================
// 保存・照合
// ============================================================================
void FormationEditorWindow::DrawAssetControls(EditorContext &context, Data &formation,
                                              const std::string &path)
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

    if (ImGui::Button("C++実装と照合する", ImVec2(180, 24)))
        RunAbCheck(context, DroneFormationSystem::SpecAssets()[m_editTarget].type);
    Hint("100機 × 進行度21点で C++ 実装と座標を突き合わせ、最大のずれを出す。\n"
         "0.01m 未満なら画面では区別が付かない＝再現できている。");

    if (!m_abDone) return;

    if (m_abMaxError < 0.0f)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "片方の実装しか無い（比較できない）");
        return;
    }

    const bool match = (m_abMaxError < 0.01f);
    ImGui::TextColored(match ? ImVec4(0.4f, 1.0f, 0.5f, 1.0f) : ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                       "%s  最大ずれ %.4f m", match ? "一致" : "ずれあり", m_abMaxError);
    if (!match)
        ImGui::TextDisabled("最悪 localIndex=%d / 進行度=%.2f", m_abWorstIndex, m_abWorstProgress);
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
    FormationContext ctx;
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
