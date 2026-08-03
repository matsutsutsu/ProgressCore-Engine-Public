#include "JoltSandboxGuiSystem.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

// 必要なコンポーネント群をインクルード
#include "Game/Core/AllComponents.h"
#include "Engine/Serialization/Factory/EntityFactory.h"

#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif
#include <random>

// ArchetypeHelper 等へのアクセスを簡略化するため名前空間を展開
using namespace CCL::ECS;

#ifdef ENABLE_EDITOR
void JoltSandboxGuiSystem::OnGui()
{
    // イムグイのウィンドウを作成
    if (!ImGui::Begin("Jolt Physics Sandbox"))
    {
        ImGui::End();
        return;
    }

    // タブバーで多機能を整理
    if (ImGui::BeginTabBar("SandboxTabs"))
    {

        // ========================================================
        // タブ1: Generators (塊で出す)
        // ========================================================
        if (ImGui::BeginTabItem("Generators"))
        {

            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Mass Avalanche (雪崩)");
            ImGui::SliderInt("Count", &m_spawnCount, 10, 5000);
            ImGui::SliderFloat("Drop Height", &m_spawnHeight, 20.0f, 200.0f);
            ImGui::SliderFloat("Spread Radius", &m_spreadRadius, 5.0f, 100.0f);

            if (ImGui::Button("Spawn Avalanche!", ImVec2(-1, 30)))
            {
                SpawnAvalanche();
            }

            ImGui::Separator();

            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "Block Tower (タワー)");
            ImGui::SliderInt("Floors", &m_towerFloors, 3, 50);
            if (ImGui::Button("Spawn Tower", ImVec2(-1, 30)))
            {
                SpawnBlockTower();
            }

            ImGui::EndTabItem();
        }

        // ========================================================
        // タブ2: Fun Tools (面白いツール)
        // ========================================================
        if (ImGui::BeginTabItem("Fun Tools"))
        {

            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Meteor Strike (巨大隕石)");
            ImGui::SliderFloat("Meteor Size", &m_meteorSize, 5.0f, 50.0f);
            if (ImGui::Button("Drop Meteor", ImVec2(-1, 40)))
            {
                SpawnMeteorStrike();
            }
            ImGui::TextWrapped("巨大な質量の球体を上空から落下させ、下のオブジェクトを吹き飛ばします。");

            ImGui::Separator();

            if (ImGui::Button("Spawn Anti-Gravity Box (上へ落ちる箱)"))
            {
                // 重力係数をマイナスにした特殊な箱を1つスポーン
                auto arch = ArchetypeHelper::Generate<TransformComponent, PrimitiveComponent, JoltRigidbodyComponent, JoltBoxColliderComponent>();
                auto e    = EntityFactory::SpawnNamed(*m_world, "AntiGravityBox", arch);

                e.Set(TransformComponent{.position = {0, 5.0f, 0}, .rotation = {0, 0, 0, 1}, .scale = {2.0f, 2.0f, 2.0f}, .isStatic = false, .isDirty = true});
                e.Set(PrimitiveComponent{.type = PrimitiveType::Box, .color = {0.0f, 1.0f, 0.5f, 1.0f}});
                e.Set(JoltBoxColliderComponent{.halfExtent = {1.0f, 1.0f, 1.0f}});

                JoltRigidbodyComponent rigid;
                rigid.motionType    = JPH::EMotionType::Dynamic;
                rigid.gravityFactor = -0.5f; // 重力を反転（上に落ちる）
                e.Set(rigid);
                m_sandboxEntities.push_back(e.ID());
            }

            ImGui::EndTabItem();
        }

        // ========================================================
        // タブ3: Management (管理・消去)
        // ========================================================
        if (ImGui::BeginTabItem("Manage"))
        {

            ImGui::Text("Active Sandbox Entities: %zu", m_sandboxEntities.size());

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            if (ImGui::Button("Clear All Sandbox Entities", ImVec2(-1, 50)))
            {
                ClearAllSandboxEntities();
            }
            ImGui::PopStyleColor(2);

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}
#else
void JoltSandboxGuiSystem::OnGui() {}
#endif

// ------------------------------------------------------------------
// 各種スポーン実装
// ------------------------------------------------------------------

void JoltSandboxGuiSystem::SpawnAvalanche()
{
    std::mt19937                          gen(std::random_device{}());
    std::uniform_real_distribution<float> distRadius(-m_spreadRadius, m_spreadRadius);
    std::uniform_real_distribution<float> distHeight(0.0f, m_spawnHeight * 0.5f);
    std::uniform_real_distribution<float> distColor(0.2f, 1.0f);

    auto arch = ArchetypeHelper::Generate<TransformComponent, PrimitiveComponent, JoltRigidbodyComponent, JoltBoxColliderComponent>();

    for (int i = 0; i < m_spawnCount; ++i)
    {
        // CCL::ECS:: を削除し、直接 EntityFactory::SpawnNamed を呼ぶ
        auto e = EntityFactory::SpawnNamed(*m_world, "AvalancheDebris", arch);

        float x = distRadius(gen);
        float y = m_spawnHeight + distHeight(gen);
        float z = distRadius(gen);

        e.Set(TransformComponent{
            .position = {x, y, z}, .rotation = {0, 0, 0, 1}, .scale = {1.0f, 1.0f, 1.0f}, .isStatic = false, .isDirty = true});
        e.Set(PrimitiveComponent{.type = PrimitiveType::Box, .color = {distColor(gen), distColor(gen), distColor(gen), 1.0f}});
        e.Set(JoltBoxColliderComponent{.halfExtent = {0.5f, 0.5f, 0.5f}});

        // JPH::EMotionType::Dynamic を明示的に指定する
        JoltRigidbodyComponent rigid;
        rigid.motionType  = JPH::EMotionType::Dynamic;
        rigid.friction    = 0.5f;
        rigid.restitution = 0.4f;
        e.Set(rigid);

        m_sandboxEntities.push_back(e.ID());
    }
}

void JoltSandboxGuiSystem::SpawnBlockTower()
{
    auto arch = ArchetypeHelper::Generate<TransformComponent, PrimitiveComponent, JoltRigidbodyComponent, JoltBoxColliderComponent>();

    float blockSize = 2.0f;
    float offset    = blockSize * 0.5f;

    for (int y = 0; y < m_towerFloors; ++y)
    {
        bool isEven = (y % 2 == 0);
        for (int i = -1; i <= 1; ++i)
        {

            auto e = EntityFactory::SpawnNamed(*m_world, "TowerBlock", arch);

            float posX = isEven ? (i * blockSize) : 0.0f;
            float posZ = isEven ? 0.0f : (i * blockSize);
            float posY = offset + (y * blockSize);

            float scaleX = isEven ? 0.95f : 3.0f;
            float scaleZ = isEven ? 3.0f : 0.95f;

            e.Set(TransformComponent{
                .position = {posX, posY, posZ}, .rotation = {0, 0, 0, 1}, .scale = {scaleX, 1.0f, scaleZ}, .isStatic = false, .isDirty = true});
            e.Set(PrimitiveComponent{.type = PrimitiveType::Box, .color = {0.8f, 0.6f, 0.4f, 1.0f}});
            e.Set(JoltBoxColliderComponent{.halfExtent = {scaleX * 0.5f, 0.5f, scaleZ * 0.5f}});

            JoltRigidbodyComponent rigid;
            rigid.motionType = JPH::EMotionType::Dynamic;
            rigid.friction   = 0.8f;
            e.Set(rigid);

            m_sandboxEntities.push_back(e.ID());
        }
    }
}

void JoltSandboxGuiSystem::SpawnMeteorStrike()
{
    auto arch = ArchetypeHelper::Generate<TransformComponent, PrimitiveComponent, JoltRigidbodyComponent, JoltSphereColliderComponent>();

    auto e = EntityFactory::SpawnNamed(*m_world, "Meteor", arch);

    e.Set(TransformComponent{
        .position = {0.0f, m_spawnHeight + 50.0f, 0.0f}, .rotation = {0, 0, 0, 1}, .scale = {m_meteorSize, m_meteorSize, m_meteorSize}, .isStatic = false, .isDirty = true});
    e.Set(PrimitiveComponent{.type = PrimitiveType::Sphere, .color = {1.0f, 0.3f, 0.0f, 1.0f}});
    e.Set(JoltSphereColliderComponent{.radius = m_meteorSize * 0.5f});

    JoltRigidbodyComponent rigid;
    rigid.motionType      = JPH::EMotionType::Dynamic;
    rigid.gravityFactor   = 3.0f;
    rigid.initialVelocity = {0.0f, -50.0f, 0.0f};
    e.Set(rigid);

    m_sandboxEntities.push_back(e.ID());
}

void JoltSandboxGuiSystem::ClearAllSandboxEntities()
{
    for (auto id : m_sandboxEntities)
    {
        if (m_world->IsEntityValid(id))
        {
            m_world->AddComponent<Tag::DestroyTag>(id);
        }
    }
    m_sandboxEntities.clear();
}

// Logicシステムの最後尾あたり（UI描画フェーズの直前など）に登録
REGISTER_LOGIC_SYSTEM(JoltSandboxGuiSystem, Priority::LogicStage::L02_PostUpdate);
