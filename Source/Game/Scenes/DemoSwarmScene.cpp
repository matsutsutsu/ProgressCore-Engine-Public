#include "DemoSwarmScene.h"
#include "Game/Logics/Demo/DemoSwarmSystem.h"

#include "Engine/Scene/SceneFactory.h"
#include "Engine/Serialization/Serializer/LevelSerializer.h"
#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Core/Components/PrefabInstanceComponent.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"
#include "tracy/Tracy.hpp"

#include <thread>

#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif

using namespace CCL::ECS;

namespace
{
    constexpr const char *kLevelPath   = "Assets/Scene/TestScene/DemoSwarm.level.json";
    // 隊長機だけドローンのモデルにし、一般機はキューブにする。
    // ドローンは1機で約2万頂点（ファイルサイズからの概算）あり、千機を超えると描画の重さで
    // 更新処理の速さが見えなくなるため
    constexpr const char *kLeaderPrefab   = "Assets/Prefabs/Demo/DemoSwarmDrone.json"; // モデルだけのドローン
    constexpr const char *kFollowerPrefab = "Assets/Prefabs/Demo/DemoSwarmCube.json";  // 箱のプリミティブ

    // 隊長機の大きさの倍率。参考にした ECS デモと同じく、一般機（1辺 0.1m）の10倍の約1mにする
    // （ドローンのモデルは約3mなので 1/3）
    constexpr float kLeaderScale = 1.0f / 3.0f;

    // 生成する機数の下限（隊長機1機と、一般機が少なくとも1機）
    constexpr int kMinCount = 2;

    // PrimitiveRenderer が1スレッドぶんの描画リストで受け付ける上限（PrimitiveRenderer.cpp）
    constexpr int kPrimitiveListLimit = 100000;
} // namespace

// ---------------------------------------------------------------------------
// 生成・破棄
// ---------------------------------------------------------------------------

void DemoSwarmScene::Initialize()
{
    BaseScene::Initialize(); // リソースの登録と RegisterSystems

    {
        ZoneScopedN("DemoSwarmScene: LoadLevel");
        if (!LevelSerializer::LoadLevel(m_worldPtr, kLevelPath))
            CCL_LOG_ERROR(LogCategory::Core, "DemoSwarmScene: レベルの読み込みに失敗: %s", kLevelPath);
    }

    RespawnSwarm(static_cast<uint32_t>(m_requestedCount));
}

void DemoSwarmScene::Finalize()
{
    m_drones.clear();
    m_swarmSystem = nullptr;
    BaseScene::Finalize();
}

void DemoSwarmScene::RegisterSystems()
{
    auto &sysManager = *(m_ecsContext->GetSystemManager());
    CCL::ECS::SystemRegistry::Instance().RegisterAll(sysManager);

    // デモでだけ動かすので、全体登録（REGISTER_*）せずここで登録する
    m_swarmSystem = sysManager.RegisterSystem<DemoSwarmSystem>(Priority::LogicStage::L02_Update);
}

void DemoSwarmScene::RespawnSwarm(uint32_t count)
{
    // 処理フロー:
    // 1. 今の群れを消す
    // 2. count 機を生成し、番号を振って最初の位置と速度を与える
    // 3. 生成を確定させ、System を最初の状態に戻す

    ZoneScopedN("DemoSwarmScene::RespawnSwarm");

    // 最初の位置と速度は System の調整値で決まるので、System が無ければ作れない
    if (!m_swarmSystem) return;

    // --- 今の群れを消す ---

    for (EntityID id : m_drones)
        m_worldPtr->Destroy(id);
    m_drones.clear();
    m_drones.reserve(count);

    // --- 生成 ---

    for (uint32_t i = 0; i < count; ++i)
    {
        const bool     isLeader = (i == 0);
        const EntityID id       = Prefab::SpawnPrefab(*m_worldPtr, isLeader ? kLeaderPrefab : kFollowerPrefab);
        if (id == InvalidEntityID) continue;

        DirectX::SimpleMath::Vector3 pos, vel;
        m_swarmSystem->InitialState(i, count, pos, vel);

        // 最初から塊の中の位置に置く。
        // prevPosition もそろえて、描画補間で1フレームだけ原点から伸びるのを防ぐ
        if (auto *trans = m_worldPtr->GetComponent<TransformComponent>(id))
        {
            trans->position     = pos;
            trans->prevPosition = pos;
            trans->isDirty      = true;

            // 隊長機は大きくして、群れの中で見分けられるようにする
            if (isLeader)
            {
                trans->scale.x *= kLeaderScale;
                trans->scale.y *= kLeaderScale;
                trans->scale.z *= kLeaderScale;
            }
        }

        // プレハブの参照を消す。残したままエディタで Save Level すると、
        // 生成した機体がレベルに配置として書き込まれてしまう
        if (auto *inst = m_worldPtr->GetComponent<PrefabInstanceComponent>(id))
            inst->prefabPath.clear();

        m_worldPtr->AddComponent<DemoSwarmComponent>(id, DemoSwarmComponent{i, vel});
        m_drones.push_back(id);
    }

    // --- 確定 ---

    // AddComponent は予約なので、ここで反映しないと最後の1機に番号が付かないまま次の更新に入る
    m_worldPtr->ScrutinyAndApply();

    m_swarmSystem->Reset(count);

    CCL_LOG_INFO(LogCategory::Game, "[DemoSwarm] %u 機を生成", count);
}

// ---------------------------------------------------------------------------
// 毎フレーム
// ---------------------------------------------------------------------------

void DemoSwarmScene::Update(float elapsedTime)
{
    // 作り直しは ImGui の描画中ではなく、ここ（System が走っていない所）で行う
    if (m_pendingCount >= 0)
    {
        RespawnSwarm(static_cast<uint32_t>(m_pendingCount));
        m_pendingCount = -1;
    }

    BaseScene::Update(elapsedTime);
}

void DemoSwarmScene::OnDrawImGui()
{
#ifdef ENABLE_EDITOR
    if (!m_swarmSystem) return;

    if (ImGui::Begin("群れデモ"))
    {
        ImGui::Text("機数 %u", m_swarmSystem->GetDroneCount());

        // 上限は設けない。ボタンの +/- は 1000 刻み（Ctrl を押しながらで 10000 刻み）
        if (ImGui::InputInt("生成する機数", &m_requestedCount, 1000, 10000))
            m_requestedCount = (std::max)(m_requestedCount, kMinCount);
        if (ImGui::Button("この機数で作り直す")) m_pendingCount = m_requestedCount;

        // PrimitiveRenderer は1スレッドぶんの描画リストに 10 万個を超えて積まれると、全部捨てて描かない。
        // 直列だと1スレッドに全機が積まれるので、それを超える機数では何も映らなくなる
        if (m_requestedCount > kPrimitiveListLimit)
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                               "※ %d 機を超えると、直列のときキューブが描画されません", kPrimitiveListLimit);

        ImGui::Separator();

        // この System だけを切り替える。全体を切ると他の System の時間も混ざり、比べにくくなる
        bool multiThread = m_swarmSystem->enableMultiThread;
        if (ImGui::Checkbox("並列処理（JobSystem）", &multiThread))
            m_swarmSystem->enableMultiThread = multiThread;

        ImGui::Text("群れの更新 %.3f ms", m_swarmSystem->GetUpdateMs());

        // ForEachParallel は Chunk 数がコア数×2 未満だと直列に逃げる（CCL_IfSystem.h）。
        // 並列のつもりで撮って実は直列だった、を防ぐために実際どちらで動いているかを出す
        unsigned int threads = std::thread::hardware_concurrency();
        if (threads == 0) threads = 4;

        const size_t chunks   = m_swarmSystem->GetChunkCount();
        const bool   parallel = multiThread && chunks >= static_cast<size_t>(threads) * 2;
        ImGui::Text("Chunk %zu 個（並列になる下限 %u 個）", chunks, threads * 2);
        ImGui::Text("→ %s", parallel ? "並列で実行中" : "直列で実行中");

        // --- 動きの調整 ---

        // 長さと強さは「最初の塊の一辺」に対する割合（DemoSwarmTuning の説明を参照）
        if (ImGui::CollapsingHeader("動きの調整", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DemoSwarmTuning &tune = m_swarmSystem->tuning;

            ImGui::SeparatorText("隊長機");
            ImGui::SliderFloat("中心点へ引く強さ", &tune.leaderPull, 0.0f, 1.0f);
            ImGui::SliderFloat("ループの大きさ（作り直すと反映）", &tune.leaderOrbit, 0.5f, 50.0f);

            ImGui::SeparatorText("一般機");
            ImGui::SliderFloat("追いかける強さ", &tune.followerPull, 0.0f, 2.0f);
            ImGui::SliderFloat("空気抵抗", &tune.followerDrag, 0.0f, 2.0f);

            ImGui::Checkbox("隊長機の周りを回る目標点を追う", &tune.useOrbitTarget);
            ImGui::BeginDisabled(!tune.useOrbitTarget);
            ImGui::DragFloatRange2("目標点の距離", &tune.orbitRadiusMin, &tune.orbitRadiusMax, 0.01f, 0.0f, 5.0f);
            ImGui::DragFloatRange2("目標点の回る速さ", &tune.orbitSpeedMin, &tune.orbitSpeedMax, 0.01f, 0.0f, 5.0f);
            ImGui::EndDisabled();

            if (ImGui::Button("初期値に戻す")) tune = DemoSwarmTuning{};
        }
    }
    ImGui::End();
#endif
}

// SceneFactory へ登録する（SceneManager がこのクラスを include せずに済むように）
REGISTER_SCENE(DemoSwarmScene, "DemoSwarm")
