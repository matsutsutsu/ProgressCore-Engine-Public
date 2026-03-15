#include "Prefab.h"
#include "Engine/Serialization/SceneSerializer.h" // 汎用シリアライザ
#include "Game/Core/AllComponents.h"
#include <SimpleMath.h>

// これを入れないと get_to(config) が動きません
#include "Engine/Serialization/ComponentSerializers.h"
#include "Engine/GamePlay/Transform/TransformUpdateSystem.h"

// インクルード
#include <fstream>
#include <iostream>
#include <json.hpp>
#include <unordered_map>
#include <windows.h>
#include <filesystem> // ★追加: ファイル存在確認用
#include <sstream> // エラーメッセージ構築用

using namespace DirectX::SimpleMath;

namespace Prefab {

    // =========================================================
    // 内部構造体
    // =========================================================
    struct ParticleNode {
        ParticleSystemConfig config;
        std::string          texturePath;
        std::string          colorRampPath;
        DirectX::XMFLOAT3    offsetPosition = {0, 0, 0};
    };

    struct CompositeBlueprint {
        std::vector<ParticleNode> nodes;
    };

    namespace {
        ID3D11Device *g_device = nullptr;

        // パーティクル用キャッシュ
        std::unordered_map<std::string, CompositeBlueprint>    g_particleCache;
        std::unordered_map<std::string, std::vector<EntityID>> g_particlePool;
    } // namespace

    void Cleanup()
    {
        g_particlePool.clear();
        g_particleCache.clear();
        g_device = nullptr;
    }

    void Initialize(ID3D11Device *device)
    {
        g_device = device;
        // RegisterAllPrefabs はもう不要なので削除
    }

    // =========================================================
    // ヘルパー: 設計図の取得 (変更なし)
    // =========================================================
    const CompositeBlueprint &GetCachedCompositeBlueprint(const std::string &path)
    {
        auto it = g_particleCache.find(path);
        if (it != g_particleCache.end()) {
            return it->second;
        }

        CompositeBlueprint bp;
        std::ifstream      i(path);
        if (i.is_open()) {
            try {
                json root;
                i >> root;
                if (root.contains("entities") && root["entities"].is_array()) {
                    for (auto &entity : root["entities"]) {
                        if (entity.contains("components")) {
                            auto &comps = entity["components"];
                            if (comps.contains("GPUParticle")) {
                                ParticleNode node;
                                auto        &pData = comps["GPUParticle"];
                                if (pData.contains("config")) pData["config"].get_to(node.config);
                                if (pData.contains("maxParticles"))
                                    node.config.max_particle_count = pData["maxParticles"];
                                else if (node.config.max_particle_count <= 0)
                                    node.config.max_particle_count = 10000;
                                node.texturePath   = pData.value("texturePath", "");
                                node.colorRampPath = pData.value("colorRampPath", "");
                                if (comps.contains("Transform")) {
                                    auto &tData = comps["Transform"];
                                    if (tData.contains("position") &&
                                        tData["position"].is_array()) {
                                        node.offsetPosition.x = tData["position"][0];
                                        node.offsetPosition.y = tData["position"][1];
                                        node.offsetPosition.z = tData["position"][2];
                                    }
                                }
                                bp.nodes.push_back(node);
                            }
                        }
                    }
                }
            }
            catch (...) {
            }
        }
        g_particleCache[path] = bp;
        return g_particleCache[path];
    }

    // =====================================================================
    // プール返却 (変更なし)
    // =====================================================================
    void ReturnParticleToPool(Core::World &world, EntityID id)
    {
        auto *pooled = world.GetComponent<PooledParticleComponent>(id);
        if (!pooled) {
            world.Destroy(id);
            return;
        }
        pooled->isSleeping = true;

        if (auto *t = world.GetComponent<TransformComponent>(id)) {
            t->position = DirectX::XMFLOAT3(0, -9999, 0);
            TransformUpdateSystem::SetParent(world, id, 0);
        }
        if (auto *p = world.GetComponent<GPUParticleComponent>(id)) {
            p->config.time = 0.0f;
        }
        g_particlePool[pooled->poolKey].push_back(id);
    }

    // =====================================================================
    // パーティクル生成 (変更なしだが、プール再利用時のPending処理を追加済み)
    // =====================================================================
    EntityID SpawnParticle(Core::World &world,
        const std::string              &path,
        EntityID                        parentID,
        const DirectX::XMFLOAT3        &localPos)
    {
        const CompositeBlueprint &blueprint = GetCachedCompositeBlueprint(path);
        if (blueprint.nodes.empty()) return 0;

        EntityID lastID = 0;

        for (size_t i = 0; i < blueprint.nodes.size(); ++i) {
            const auto &node       = blueprint.nodes[i];
            std::string poolKey    = path + "#" + std::to_string(i);
            EntityID    particleID = 0;

            // A. プールから再利用
            if (!g_particlePool[poolKey].empty()) {
                particleID = g_particlePool[poolKey].back();
                g_particlePool[poolKey].pop_back();

                if (auto *pooled = world.GetComponent<PooledParticleComponent>(particleID)) {
                    pooled->isSleeping = false;
                }

                bool parentSetSuccess = false;
                if (auto *trans = world.GetComponent<TransformComponent>(particleID)) {
                    TransformUpdateSystem::SetParent(world, particleID, parentID);
                    if (parentID != 0 && trans->parentID == parentID) {
                        parentSetSuccess = true;
                    }
                    trans->position.x = localPos.x + node.offsetPosition.x;
                    trans->position.y = localPos.y + node.offsetPosition.y;
                    trans->position.z = localPos.z + node.offsetPosition.z;
                }



                if (parentID != 0 && !parentSetSuccess) {
                    if (!world.HasComponent<PendingParentComponent>(particleID)) {
                        PendingParentComponent pending;
                        pending.parentID = parentID;
                        world.AddComponent<PendingParentComponent>(particleID, pending);
                    }
                }

                if (auto *gpuP = world.GetComponent<GPUParticleComponent>(particleID)) {
                    gpuP->config = node.config;
                    if (!node.texturePath.empty())
                        strcpy_s(
                            gpuP->texturePath, sizeof(gpuP->texturePath), node.texturePath.c_str());
                    if (!node.colorRampPath.empty())
                        strcpy_s(gpuP->colorRampPath,
                            sizeof(gpuP->colorRampPath),
                            node.colorRampPath.c_str());
                    if (gpuP->maxParticles < node.config.max_particle_count) {
                        gpuP->maxParticles  = node.config.max_particle_count;
                        gpuP->isInitialized = false;
                    }
                }
            }
            // B. 新規作成
            else {
                auto arch = ArchetypeHelper::
                    Generate<TransformComponent, GPUParticleComponent, PooledParticleComponent>();
                particleID = world.RequestSpawnEntity(arch);

                TransformComponent trans;
                trans.position.x = localPos.x + node.offsetPosition.x;
                trans.position.y = localPos.y + node.offsetPosition.y;
                trans.position.z = localPos.z + node.offsetPosition.z;
                world.AddComponent<TransformComponent>(particleID, std::move(trans));

                if (parentID != 0) {
                    PendingParentComponent pending;
                    pending.parentID = parentID;
                    world.AddComponent<PendingParentComponent>(particleID, pending);
                }

                GPUParticleComponent particle;
                particle.config = node.config;
                if (!node.texturePath.empty())
                    strcpy_s(particle.texturePath,
                        sizeof(particle.texturePath),
                        node.texturePath.c_str());
                if (!node.colorRampPath.empty())
                    strcpy_s(particle.colorRampPath,
                        sizeof(particle.colorRampPath),
                        node.colorRampPath.c_str());
                // 初期化は UpdateSystem がやるので、数値を設定するだけ
                particle.maxParticles = particle.config.max_particle_count;
                particle.needsGpuInit = true;
                world.AddComponent<GPUParticleComponent>(particleID, std::move(particle));

                PooledParticleComponent poolTag;
                poolTag.SetKey(poolKey);
                world.AddComponent<PooledParticleComponent>(particleID, std::move(poolTag));
            }

            float maxLife        = node.config.lifespan.y;
            float targetLifeTime = (parentID == 0) ? (maxLife + 1.0f) : 0.0f;

            if (auto *timer = world.GetComponent<TimerComponent>(particleID)) {
                timer->lifeTime    = targetLifeTime;
                timer->currentTime = 0.0f;
                timer->isExpired   = false;
            }
            else if (targetLifeTime > 0.0f) {
                TimerComponent newTimer;
                newTimer.lifeTime    = targetLifeTime;
                newTimer.currentTime = 0.0f;
                world.AddComponent<TimerComponent>(particleID, std::move(newTimer));
            }
            lastID = particleID;
        }
        return lastID;
    }

    void PrewarmParticles(Core::World &world)
    {
      /*  std::vector<EntityID> allCreatedIDs;
        std::string           firePath = "Assets/GamePlay/Prefabs/Particles/VFX_Env_Fire.json";
        for (int i = 0; i < 20; ++i)
            allCreatedIDs.push_back(SpawnParticle(world, firePath, 0, {0, 0, 0}));
        std::string bulletPath = "Assets/GamePlay/Prefabs/Particles/VFX_Weapon_Bullet.json";
        for (int i = 0; i < 40; ++i)
            allCreatedIDs.push_back(SpawnParticle(world, bulletPath, 0, {0, 0, 0}));
        std::string explPath = "Assets/GamePlay/Prefabs/Particles/VFX_Explosion_Base.json";
        for (int i = 0; i < 40; ++i)
            allCreatedIDs.push_back(SpawnParticle(world, explPath, 0, {0, 0, 0}));

        world.ScrutinyAndApply();

        for (EntityID id : allCreatedIDs) ReturnParticleToPool(world, id);
        world.ScrutinyAndApply();
        OutputDebugStringA("[Prefab] Particles Pre-warmed.\n");*/
    }

    // ===================================================================================
    // 実装: 汎用SpawnPrefab (SceneSerializerを使う)
    // ===================================================================================
    EntityID SpawnPrefab(Core::World &world, const std::string &prefabPath)
    {
        std::string fullPath = prefabPath;
        if (fullPath.find(".json") == std::string::npos) {
            fullPath += ".json";
        }

        // ---------------------------------------------------------
        // 追加: ファイル存在チェックとエラーハンドリング
        // ---------------------------------------------------------
        if (!std::filesystem::exists(fullPath)) {
            // 1. デバッグ出力ウィンドウにエラーを表示
            std::string err = "[Error] Prefab Not Found: " + fullPath + "\n";
            OutputDebugStringA(err.c_str());

            // 2. コンソールにも出す（必要なら）
            // std::cout << err << std::endl;

            // 3. 失敗として無効なIDを返す
            return 0; // または CCL::ECS::InvalidEntityID
        }
        // ---------------------------------------------------------

        // シリアライザを使ってロード
        // JSON内に書かれているコンポーネントが全て復元されます
        std::vector<EntityID> createdEntities = SceneSerializer::Deserialize(&world, fullPath);

        if (createdEntities.empty()) return 0;

        // ルートエンティティ(親なし)を探して返す
        // ※Deserializeが返したリストの0番目がルートとは限らないが、通常はシーンファイルなら0番目
        // 厳密には parentID == 0 を探す
        EntityID rootID = createdEntities[0];
        for (EntityID id : createdEntities) {
            auto *t = world.GetComponent<TransformComponent>(id);
            if (t && t->parentID == 0) {
                rootID = id;
                break;
            }
        }
        return rootID;
    }

    EntityID SpawnPrefab(
        Core::World &world, const std::string &path, const DirectX::XMFLOAT3 &position)
    {
        EntityID entity = SpawnPrefab(world, path);
        if (entity != CCL::ECS::InvalidEntityID) {
            world.PatchComponent<TransformComponent>(
                entity, [position](TransformComponent &trans) { trans.position = position; });
        }
        return entity;
    }

    EntityID SpawnPrefab(Core::World &world,
        const std::string            &path,
        const DirectX::XMFLOAT3      &position,
        const DirectX::XMFLOAT4      &rotation)
    {
        EntityID entity = SpawnPrefab(world, path);
        if (entity != CCL::ECS::InvalidEntityID) {
            world.PatchComponent<TransformComponent>(
                entity, [position, rotation](TransformComponent &trans) {
                    trans.position = position;
                    trans.rotation = rotation;
                });
        }
        return entity;
    }

    EntityID SpawnPrefab(Core::World &world,
        const std::string            &path,
        const DirectX::XMFLOAT3      &position,
        float                         rotationY)
    {
        EntityID entity = SpawnPrefab(world, path);
        if (entity != CCL::ECS::InvalidEntityID) {
            world.PatchComponent<TransformComponent>(
                entity, [position, rotationY](TransformComponent &trans) {
                    trans.position          = position;
                    DirectX::XMVECTOR quatY = DirectX::XMQuaternionRotationRollPitchYaw(
                        0.0f, DirectX::XMConvertToRadians(rotationY), 0.0f);
                    DirectX::XMStoreFloat4(&trans.rotation, quatY);
                });
        }
        return entity;
    }

} // namespace CCL::Prefab