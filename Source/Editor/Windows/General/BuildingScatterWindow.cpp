#include "Editor/Windows/General/BuildingScatterWindow.h"

#include "Engine/Core/Components/NameComponent.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Graphics/Components/Core/MaterialComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/ECS/Common/CCL_Archetype.h"
#include "Engine/Serialization/Factory/EntityFactory.h"
#include "Engine/Transform/Systems/TransformUpdateSystem.h"
#include "Engine/Platform/Core/Logger.h"

#include <imgui.h>
#include <DirectXMath.h>
#include <random>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace CCL::ECS;

namespace
{
    constexpr float kTwoPi = 6.28318530718f;

    // 建物モデルのパスを作る（modelIndex0 は 0〜7 → Scifi-Building_01〜08）
    std::string BuildModelPath(int modelIndex0)
    {
        char path[160];
        sprintf_s(path, "Assets/Models/Building/Scifi-Building_%02d.glb", modelIndex0 + 1);
        return std::string(path);
    }
}

// =========================================================================
// UI
// =========================================================================
void BuildingScatterWindow::DrawContents(EditorContext &context)
{
    ImGui::TextWrapped(
        "中心(円柱ステージ)の周囲に建物モデル(Scifi-Building_01〜08)を"
        "円環状にランダム配置します。最短距離より内側には出現しません。");
    ImGui::Separator();

    // ---- 中心と距離 ----
    ImGui::SeparatorText("配置範囲");
    ImGui::DragFloat3("中心座標", m_center, 0.1f);
    ImGui::DragFloat("最短出現距離", &m_minRadius, 0.1f, 0.0f, 100000.0f, "%.2f");
    ImGui::DragFloat("最長出現距離", &m_maxRadius, 0.1f, 0.0f, 100000.0f, "%.2f");
    ImGui::DragFloat("接地Y(中心からの高さ)", &m_baseY, 0.05f);

    // ---- 数と乱数 ----
    ImGui::SeparatorText("生成数・ばらつき");
    ImGui::DragInt("建物数", &m_count, 1.0f, 0, 100000);
    ImGui::DragInt("乱数シード", &m_seed);
    ImGui::SameLine();
    if (ImGui::SmallButton("シード変更")) m_seed = (int)(ImGui::GetTime() * 1000.0) & 0x7fffffff;
    ImGui::DragFloat("最小間隔(重なり防止)", &m_minSpacing, 0.1f, 0.0f, 100000.0f, "%.2f");

    // ---- 見た目のランダム化 ----
    ImGui::SeparatorText("見た目のランダム化");
    ImGui::Checkbox("Y軸ランダム回転", &m_randomYaw);
    ImGui::DragFloat2("スケール範囲(最小/最大)", m_scaleRange, 0.01f, 0.01f, 100.0f, "%.2f");

    // ---- 親グループ(塊) ----
    ImGui::SeparatorText("階層(まとめ方)");
    ImGui::DragInt("クラスタ(親グループ)数", &m_clusterCount, 0.1f, 1, 64);
    ImGui::TextDisabled("※ 1なら全部ひとつの親配下。2以上なら角度で塊分けし各塊を別の親にまとめます。");

    // ---- 使用モデル選択 ----
    ImGui::SeparatorText("使用する建物モデル");
    int selected = 0;
    for (int i = 0; i < 8; ++i)
    {
        char label[8];
        sprintf_s(label, "%02d", i + 1);
        ImGui::Checkbox(label, &m_useModel[i]);
        if (m_useModel[i]) ++selected;
        if (i % 4 != 3 && i != 7) ImGui::SameLine();
    }
    if (ImGui::SmallButton("全選択"))
    {
        for (auto &b : m_useModel)
            b = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("全解除"))
    {
        for (auto &b : m_useModel)
            b = false;
    }

    // ---- 入力の妥当性チェック ----
    ImGui::Separator();
    const bool hasWorld = (context.world != nullptr);
    const bool radiusOk = (m_maxRadius > m_minRadius);
    const bool modelOk  = (selected > 0);
    const bool countOk  = (m_count > 0);
    const bool canGen   = hasWorld && radiusOk && modelOk && countOk;

    if (!radiusOk) ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "・最長距離は最短距離より大きくしてください");
    if (!modelOk) ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "・建物モデルを1つ以上選択してください");
    if (!countOk) ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "・建物数は1以上にしてください");

    // ---- 生成/削除 ----
    ImGui::Spacing();
    ImGui::BeginDisabled(!canGen);
    if (ImGui::Button("生成", ImVec2(140, 32)))
    {
        Generate(context);
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(m_spawnedGroups.empty());
    if (ImGui::Button("生成物を全削除", ImVec2(160, 32)))
    {
        DeleteAll(context);
    }
    ImGui::EndDisabled();

    // ---- 状態表示 ----
    ImGui::Separator();
    ImGui::Text("生成済みグループ数: %zu", m_spawnedGroups.size());
    if (m_lastSpawnedCount > 0)
        ImGui::Text("直近の生成: %d 個の建物", m_lastSpawnedCount);
}



void BuildingScatterWindow::Generate(EditorContext &context)
{
    World *world = context.world;
    if (!world) return;

    // 使用するモデルのインデックス(0..7)を集める
    std::vector<int> models;
    for (int i = 0; i < 8; ++i)
        if (m_useModel[i]) models.push_back(i);
    if (models.empty()) return;

    const int   clusters = (std::max)(1, m_clusterCount);
    const int   count    = (std::max)(0, m_count);
    const float minR     = (std::max)(0.0f, (std::min)(m_minRadius, m_maxRadius));
    const float maxR     = (std::max)(m_minRadius, m_maxRadius);
    const float spacing2 = m_minSpacing * m_minSpacing;

    // 生成するたびに配置が変わるよう、シードに生成回数を混ぜる
    std::mt19937                          rng(static_cast<uint32_t>(m_seed) + static_cast<uint32_t>(m_batchCounter) * 2654435761u);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);

    // アーキタイプはループの外で1回だけ作る
    const auto groupArch    = ArchetypeHelper::Generate<TransformComponent, NameComponent>();
    const auto buildingArch = ArchetypeHelper::Generate<TransformComponent, ModelComponent, MaterialComponent, NameComponent>();

    // 選択モデルのマスター ModelComponent を1回だけ生成し、以降はコピーで使い回す
    // (ループ内で毎回 SetModel すると数百回パス検索/ロードが走るため)
    ModelComponent baseModels[8];
    for (int mi : models)
    {
        baseModels[mi].SetModel(BuildModelPath(mi).c_str());
    }

    // ---- 1. クラスタごとの親グループを生成 ----
    std::vector<EntityID> groups;
    groups.reserve(clusters);
    for (int c = 0; c < clusters; ++c)
    {
        char gname[64];
        sprintf_s(gname, "BuildingGroup_B%02d_C%02d", m_batchCounter, c);
        auto gref = EntityFactory::SpawnNamed(*world, gname, groupArch);
        gref.Set(TransformComponent{
            .position = {m_center[0], m_center[1], m_center[2]},
            .rotation = {0.0f, 0.0f, 0.0f, 1.0f},
            .scale    = {1.0f, 1.0f, 1.0f}});
        groups.push_back(gref.id);
        m_spawnedGroups.push_back(gref.id);
    }

    // ---- 2. 建物を生成（親子付けは実体化した後に行う） ----
    struct Placed
    {
        float x, z;
    };
    std::vector<Placed> placed;
    placed.reserve(count);

    std::vector<std::pair<EntityID, EntityID>> childToGroup; // (建物, 所属グループ)
    childToGroup.reserve(count);

    for (int i = 0; i < count; ++i)
    {
        const int c = i % clusters; // 親グループへ順番に振り分ける

        // 最小間隔を満たす場所を最大30回探す（見つからなければ最後の候補を使う）
        float px = m_center[0], pz = m_center[2];
        for (int attempt = 0; attempt < 30; ++attempt)
        {
            // 面積あたりの密度が均一になるよう、半径は二乗で補間する
            const float r = std::sqrt(minR * minR + u01(rng) * (maxR * maxR - minR * minR));
            // このクラスタの角度の範囲内に散らす（clusters>1 なら塊が空間的にも分かれる）
            const float ang = (static_cast<float>(c) + u01(rng)) / static_cast<float>(clusters) * kTwoPi;
            px              = m_center[0] + r * std::cos(ang);
            pz              = m_center[2] + r * std::sin(ang);

            bool ok = true;
            for (const auto &p : placed)
            {
                const float dx = px - p.x, dz = pz - p.z;
                if (dx * dx + dz * dz < spacing2)
                {
                    ok = false;
                    break;
                }
            }
            if (ok) break;
        }
        placed.push_back({px, pz});

        // 親グループ（中心に置く）からのオフセット
        const float lx = px - m_center[0];
        const float lz = pz - m_center[2];

        // モデル・スケール・回転をランダムに決める
        const int   mi  = models[static_cast<size_t>(rng() % models.size())];
        const float s   = m_scaleRange[0] + u01(rng) * (m_scaleRange[1] - m_scaleRange[0]);
        const float yaw = m_randomYaw ? (u01(rng) * kTwoPi) : 0.0f;

        DirectX::XMFLOAT4 rot;
        DirectX::XMStoreFloat4(&rot, DirectX::XMQuaternionRotationRollPitchYaw(0.0f, yaw, 0.0f));

        // 名前は通し番号とモデル番号で必ず一意にする
        char bname[64];
        sprintf_s(bname, "Bld_%04d_S%02d", m_nameCounter, mi + 1);
        ++m_nameCounter;

        auto ref = EntityFactory::SpawnNamed(*world, bname, buildingArch);
        ref.Set(TransformComponent{
                    .position = {lx, m_baseY, lz},
                    .rotation = rot,
                    .scale    = {s, s, s}})
            .Set(ModelComponent{baseModels[mi]}); // 最初に作ったものをコピーする（各自が別のインスタンスを持つ）

        childToGroup.emplace_back(ref.id, groups[c]);
    }

    // ---- 3. 実体化してから親子付け ----
    //   SpawnNamed は PendingOp に積むだけなので、SetParent の前に確定させる
    world->ScrutinyAndApply();
    for (const auto &[child, group] : childToGroup)
    {
        TransformUpdateSystem::SetParent(*world, child, group);
    }

    m_batchCounter++;
    m_lastSpawnedCount = count;
    CCL_LOG_INFO(LogCategory::Editor,
                 "[BuildingScatter] Generated %d buildings in %d group(s).", count, clusters);
}



void BuildingScatterWindow::DeleteAll(EditorContext &context)
{
    World *world = context.world;
    if (!world) return;

    for (EntityID g : m_spawnedGroups)
    {
        // Destroy は DestroyTag を付けるだけ。HierarchyCleanupSystem が子まで
        // 再帰的に破棄する。無効IDは Destroy 側で弾かれるため手動削除後も安全
        world->Destroy(g);
    }
    const size_t n = m_spawnedGroups.size();
    m_spawnedGroups.clear();
    m_lastSpawnedCount = 0;

    CCL_LOG_INFO(LogCategory::Editor, "[BuildingScatter] Deleted %zu group(s).", n);
}
