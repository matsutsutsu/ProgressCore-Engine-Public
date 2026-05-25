// =====================================================================
// Prefab.cpp
//   プレハブ (= 永続化されたエンティティテンプレート) のスポーン処理。
//
//   公開 API は 2 系統:
//     SpawnPrefab    : 汎用 (SceneSerializer 経由でフルロード)
//     SpawnParticle  : パーティクル専用 (プール再利用で高速生成)
//
//   修正履歴 v3:
//     - json 修飾子の欠落バグを修正 (json → nlohmann::json)
//     - 内部構造を internal namespace に集約
//     - ParticlePool を class 化 (テスト容易性 / Clear の確実化)
//     - 死コード PathToRelativeUtf8 を削除
//     - <iostream>, <sstream> 等の不要 include を削除
//     - SpawnParticle のロジックを 3 つの小関数に分割
// =====================================================================
#include "Prefab.h"
#include "Engine/Serialization/Serializer/SceneSerializer.h"
#include "Game/Core/AllComponents.h"
#include "Engine/Transform/Systems/TransformUpdateSystem.h"
#include "Engine/Platform/Core/FileSystem.h"

#include <DirectXMath.h>
#include <SimpleMath.h>

#include <fstream>
#include <json.hpp>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <cmath>
#include <Windows.h>   // OutputDebugStringA のみで使用

using namespace DirectX::SimpleMath;
using nlohmann::json;   // ★ ここで一度だけ修飾子を解放 (旧 Prefab.cpp 内の json 未定義バグの根本対策)

// =====================================================================
// JSON ADL ヘルパー (ParticleSystemConfig)
//
//   ★ グローバル名前空間に置く必要がある。
//      nlohmann::json は ADL (引数依存検索) で to_json/from_json を見つけるため、
//      ParticleSystemConfig と同じ名前空間 (= グローバル) に配置する。
//      namespace の中に入れると ADL が効かなくなり、リンクは通るが
//      実行時にデフォルト変換が走って値が空になる落とし穴がある。
// =====================================================================
namespace {
    /** @brief NaN/Inf を 0.0 にサニタイズしてシリアライズ事故を防ぐ */
    inline float SafeFloat(float v) { return std::isfinite(v) ? v : 0.0f; }
}

void to_json(json& j, const ParticleSystemConfig& c)
{
    j = json{
        {"max_particle_count",  c.max_particle_count},
        {"type",                c.type},
        {"color_mode",          c.color_mode},
        {"emission_mode",       c.emission_mode},
        {"burst_count",         c.burst_count},
        {"gravity",             SafeFloat(c.gravity)},
        {"velocity_stretch",    SafeFloat(c.velocity_stretch)},
        {"noise_scale",         SafeFloat(c.noise_scale)},
        {"noise_strength",      SafeFloat(c.noise_strength)},
        {"particle_offset_y",   SafeFloat(c.particle_offset_y)},
        {"emission_stretch_x",  SafeFloat(c.emission_stretch_x)},
        {"emission_stretch_z",  SafeFloat(c.emission_stretch_z)},
        {"sprite_anim_mode",    c.sprite_anim_mode},
    };

    j["spawn_delay"] = { SafeFloat(c.spawn_delay.x),             SafeFloat(c.spawn_delay.y) };
    j["lifespan"] = { SafeFloat(c.lifespan.x),                SafeFloat(c.lifespan.y) };
    j["emission_speed"] = { SafeFloat(c.emission_speed.x),          SafeFloat(c.emission_speed.y) };
    j["emission_angular_speed"] = { SafeFloat(c.emission_angular_speed.x),  SafeFloat(c.emission_angular_speed.y) };
    j["emission_offset"] = { SafeFloat(c.emission_offset.x),         SafeFloat(c.emission_offset.y) };
    j["emission_cone_angle"] = { SafeFloat(c.emission_cone_angle.x),     SafeFloat(c.emission_cone_angle.y) };
    j["manual_color"] = { SafeFloat(c.manual_color.x), SafeFloat(c.manual_color.y),
                                    SafeFloat(c.manual_color.z), SafeFloat(c.manual_color.w) };
    j["emission_size"] = { SafeFloat(c.emission_size.x),           SafeFloat(c.emission_size.y) };
    j["fade_duration"] = { SafeFloat(c.fade_duration.x),           SafeFloat(c.fade_duration.y) };
    j["uv_scroll_speed"] = { SafeFloat(c.uv_scroll_speed.x),         SafeFloat(c.uv_scroll_speed.y) };
    j["sprite_sheet_grid"] = { c.sprite_sheet_grid.x,                  c.sprite_sheet_grid.y };
    j["particle_scale"] = { SafeFloat(c.particle_scale.x),          SafeFloat(c.particle_scale.y) };
}

void from_json(const json& j, ParticleSystemConfig& c)
{
    // 安全に配列値を読み込むテンプレート (Float2 / Float4 両対応)
    auto Read = [&](const char* key, auto& vec, int dims) {
        if (!j.contains(key) || !j[key].is_array() || j[key].size() < static_cast<size_t>(dims))
            return;
        vec.x = j[key][0];
        if (dims >= 2) vec.y = j[key][1];
        if constexpr (requires { vec.z; }) { if (dims >= 3) vec.z = j[key][2]; }
        if constexpr (requires { vec.w; }) { if (dims >= 4) vec.w = j[key][3]; }
        };

    Read("emission_speed", c.emission_speed, 2);
    Read("emission_angular_speed", c.emission_angular_speed, 2);
    Read("emission_offset", c.emission_offset, 2);
    Read("emission_cone_angle", c.emission_cone_angle, 2);
    Read("emission_size", c.emission_size, 2);
    Read("uv_scroll_speed", c.uv_scroll_speed, 2);
    Read("sprite_sheet_grid", c.sprite_sheet_grid, 2);
    Read("lifespan", c.lifespan, 2);
    Read("spawn_delay", c.spawn_delay, 2);
    Read("fade_duration", c.fade_duration, 2);
    Read("particle_scale", c.particle_scale, 2);
    Read("manual_color", c.manual_color, 4);

    // スカラー値
    auto ReadVal = [&](const char* key, auto& dst) {
        if (j.contains(key)) dst = j[key];
        };
    ReadVal("emission_mode", c.emission_mode);
    ReadVal("burst_count", c.burst_count);
    ReadVal("color_mode", c.color_mode);
    ReadVal("max_particle_count", c.max_particle_count);
    ReadVal("type", c.type);
    ReadVal("gravity", c.gravity);
    ReadVal("velocity_stretch", c.velocity_stretch);
    ReadVal("noise_scale", c.noise_scale);
    ReadVal("noise_strength", c.noise_strength);
    ReadVal("particle_offset_y", c.particle_offset_y);
    ReadVal("emission_stretch_x", c.emission_stretch_x);
    ReadVal("emission_stretch_z", c.emission_stretch_z);
    ReadVal("sprite_anim_mode", c.sprite_anim_mode);
}

// =====================================================================
// Prefab 名前空間 - 実装
// =====================================================================
namespace Prefab {

    // -----------------------------------------------------------------
    // internal: 実装詳細 (外部から見えない)
    // -----------------------------------------------------------------
    namespace internal {

        /**
         * @brief パーティクルプレハブ JSON 内の 1 ノード分の設計図。
         *
         * 1 つの GPUParticleComponent + 関連する Transform オフセットを保持する。
         * 1 プレハブファイル内に複数ノードがある場合 (複合パーティクル) は
         * CompositeBlueprint::nodes に複数並ぶ。
         */
        struct ParticleNode {
            ParticleSystemConfig config;
            std::string          texturePath;
            std::string          colorRampPath;
            DirectX::XMFLOAT3    offsetPosition{ 0, 0, 0 };
        };

        struct CompositeBlueprint {
            std::vector<ParticleNode> nodes;
        };

        // -------------------------------------------------------------
        // ParticlePool: パーティクルエンティティの再利用プール
        //
        //   外から見ると map<キー, vector<ID>> だが、いずれ
        //   - サイズ上限
        //   - 利用統計 (Acquire/Return 回数)
        //   - スレッドセーフ化
        //   などの拡張を加えやすくするため class 化しておく。
        // -------------------------------------------------------------
        class ParticlePool {
        public:
            /** @brief 指定キーのプールから 1 つ取り出す。空なら 0 を返す。 */
            CCL::ECS::EntityID Acquire(const std::string& key)
            {
                auto& stack = _pools[key];
                if (stack.empty()) return 0;
                CCL::ECS::EntityID id = stack.back();
                stack.pop_back();
                return id;
            }

            /** @brief 使い終わったエンティティをプールに返す。 */
            void Return(const std::string& key, CCL::ECS::EntityID id)
            {
                _pools[key].push_back(id);
            }

            /** @brief 全プールを空にする (シーン切替時など)。 */
            void Clear() { _pools.clear(); }

        private:
            std::unordered_map<std::string, std::vector<CCL::ECS::EntityID>> _pools;
        };

        // -------------------------------------------------------------
        // State: モジュール内のグローバル状態を 1 つに集約
        //
        //   旧版は g_device / g_particleCache / g_particlePool が
        //   別々の anonymous namespace 変数で散在していた。
        //   1 構造体にまとめることで Clear/Initialize の漏れを防ぎ、
        //   テスト時にまるごと差し替えやすくする。
        // -------------------------------------------------------------
        struct State {
            ID3D11Device* device = nullptr;
            std::unordered_map<std::string, CompositeBlueprint> blueprintCache;
            ParticlePool pool;
        };

        /** @brief モジュール状態の唯一のインスタンスを返す */
        State& GetState()
        {
            static State s;
            return s;
        }

        /**
         * @brief JSON ファイルをパースして CompositeBlueprint を構築する。
         *
         * 注意: 現状 "GPUParticle" / "Transform" のキーをハードコードして
         *       読み出している。理想的には ComponentRegistry 経由で
         *       任意コンポーネントを取れるようにすべきだが、パーティクルは
         *       性能上の理由でフルロードを避けたい (毎フレーム 100+ 発射する)。
         *       将来、「Blueprint 用の軽量デシリアライズ」を ComponentRegistry に
         *       生やせれば、この関数のハードコードを撤去できる。
         */
        CompositeBlueprint LoadBlueprint(const std::string& path)
        {
            CompositeBlueprint bp;
            // ★ 変更後: VFS経由で文字列として読み込む
            std::string jsonStr = FileSystem::ReadAllText(path);
            if (jsonStr.empty()) return bp;

            json root;
            try {
                root = json::parse(jsonStr); // 文字列からパース
            }
            catch (const std::exception&) {
                OutputDebugStringA(("[Prefab] JSON parse failed: " + path + "\n").c_str());
                return bp;
            }

            if (!root.contains("entities") || !root["entities"].is_array()) return bp;

            for (const auto& entityJson : root["entities"]) {
                if (!entityJson.contains("components")) continue;
                const auto& comps = entityJson["components"];
                if (!comps.contains("GPUParticle")) continue;

                ParticleNode node;
                const auto& pData = comps["GPUParticle"];

                if (pData.contains("config")) pData["config"].get_to(node.config);
                if (pData.contains("maxParticles")) {
                    node.config.max_particle_count = pData["maxParticles"];
                }
                else if (node.config.max_particle_count <= 0) {
                    node.config.max_particle_count = 10000; // 安全側のデフォルト
                }
                node.texturePath = pData.value("texturePath", "");
                node.colorRampPath = pData.value("colorRampPath", "");

                if (comps.contains("Transform")) {
                    const auto& tData = comps["Transform"];
                    if (tData.contains("position") && tData["position"].is_array() &&
                        tData["position"].size() >= 3) {
                        node.offsetPosition.x = tData["position"][0];
                        node.offsetPosition.y = tData["position"][1];
                        node.offsetPosition.z = tData["position"][2];
                    }
                }
                bp.nodes.push_back(std::move(node));
            }
            return bp;
        }

        /** @brief Blueprint をキャッシュから取得 (初回はロード) */
        const CompositeBlueprint& GetOrLoadBlueprint(const std::string& path)
        {
            auto& cache = GetState().blueprintCache;
            auto it = cache.find(path);
            if (it != cache.end()) return it->second;

            auto [insertedIt, _] = cache.emplace(path, LoadBlueprint(path));
            return insertedIt->second;
        }

        // -------------------------------------------------------------
        // SpawnParticle のサブ処理
        //   旧版の SpawnParticle が 110 行で責務過多だったので分割。
        // -------------------------------------------------------------

        /** @brief プールから再利用するパーティクルを「起こす」 */
        void WakeParticle(CCL::ECS::Core::World& world,
            CCL::ECS::EntityID particleID,
            const ParticleNode& node,
            CCL::ECS::EntityID parentID,
            const DirectX::XMFLOAT3& localPos)
        {
            using namespace CCL::ECS;

            if (auto* pooled = world.GetComponent<PooledParticleComponent>(particleID)) {
                pooled->isSleeping = false;
            }

            bool parentSetSuccess = false;
            if (auto* trans = world.GetComponent<TransformComponent>(particleID)) {
                TransformUpdateSystem::SetParent(world, particleID, parentID);
                if (parentID != 0 && trans->parent == parentID) {
                    parentSetSuccess = true;
                }
                trans->position.x = localPos.x + node.offsetPosition.x;
                trans->position.y = localPos.y + node.offsetPosition.y;
                trans->position.z = localPos.z + node.offsetPosition.z;
            }

            // ChunkLayout の都合で SetParent が即座に反映されなかった場合の保険
            if (parentID != 0 && !parentSetSuccess &&
                !world.HasComponent<PendingParentComponent>(particleID))
            {
                PendingParentComponent pending;
                pending.parentID = parentID;
                world.AddComponent<PendingParentComponent>(particleID, pending);
            }

            if (auto* gpuP = world.GetComponent<GPUParticleComponent>(particleID)) {
                gpuP->config = node.config;
                if (!node.texturePath.empty())
                    strcpy_s(gpuP->texturePath, sizeof(gpuP->texturePath), node.texturePath.c_str());
                if (!node.colorRampPath.empty())
                    strcpy_s(gpuP->colorRampPath, sizeof(gpuP->colorRampPath), node.colorRampPath.c_str());

                if (gpuP->maxParticles < node.config.max_particle_count) {
                    gpuP->maxParticles = node.config.max_particle_count;
                    gpuP->isInitialized = false;
                }
            }
        }

        /** @brief プールに無いので新しく生成する */
        CCL::ECS::EntityID CreateParticle(CCL::ECS::Core::World& world,
            const ParticleNode& node,
            const std::string& poolKey,
            CCL::ECS::EntityID parentID,
            const DirectX::XMFLOAT3& localPos)
        {
            using namespace CCL::ECS;

            auto arch = ArchetypeHelper::Generate<
                TransformComponent, GPUParticleComponent, PooledParticleComponent>();
            EntityID id = world.RequestSpawnEntity(arch);

            // Transform
            TransformComponent trans;
            trans.position.x = localPos.x + node.offsetPosition.x;
            trans.position.y = localPos.y + node.offsetPosition.y;
            trans.position.z = localPos.z + node.offsetPosition.z;
            world.AddComponent<TransformComponent>(id, std::move(trans));

            if (parentID != 0) {
                PendingParentComponent pending;
                pending.parentID = parentID;
                world.AddComponent<PendingParentComponent>(id, pending);
            }

            // GPUParticle
            GPUParticleComponent particle;
            particle.config = node.config;
            if (!node.texturePath.empty())
                strcpy_s(particle.texturePath, sizeof(particle.texturePath), node.texturePath.c_str());
            if (!node.colorRampPath.empty())
                strcpy_s(particle.colorRampPath, sizeof(particle.colorRampPath), node.colorRampPath.c_str());
            particle.maxParticles = particle.config.max_particle_count;
            particle.needsGpuInit = true;
            world.AddComponent<GPUParticleComponent>(id, std::move(particle));

            // PoolTag
            PooledParticleComponent poolTag;
            poolTag.SetKey(poolKey);
            world.AddComponent<PooledParticleComponent>(id, std::move(poolTag));

            return id;
        }

        /** @brief 寿命タイマーを設定 (親なしの場合は自動消滅) */
        void SetupLifetimeTimer(CCL::ECS::Core::World& world,
            CCL::ECS::EntityID particleID,
            const ParticleNode& node,
            CCL::ECS::EntityID parentID)
        {
            float maxLife = node.config.lifespan.y;
            float targetLifeTime = (parentID == 0) ? (maxLife + 1.0f) : 0.0f;

            if (auto* timer = world.GetComponent<TimerComponent>(particleID)) {
                timer->lifeTime = targetLifeTime;
                timer->currentTime = 0.0f;
                timer->isExpired = false;
            }
            else if (targetLifeTime > 0.0f) {
                TimerComponent newTimer;
                newTimer.lifeTime = targetLifeTime;
                newTimer.currentTime = 0.0f;
                world.AddComponent<TimerComponent>(particleID, std::move(newTimer));
            }
        }
    } // namespace internal


    // =================================================================
    // 公開 API
    // =================================================================

    void Cleanup()
    {
        auto& s = internal::GetState();
        s.pool.Clear();
        s.blueprintCache.clear();
        s.device = nullptr;
    }

    void Initialize(ID3D11Device* device)
    {
        internal::GetState().device = device;
    }

    void PrewarmParticles(Core::World& /*world*/)
    {
        // 以前ここで Fire / Bullet / Explosion を事前生成していたが、
        // 起動時間の悪化が顕著だったため現在は無効化。
        // 必要になったら設定ファイル駆動で復活させること。
    }

    /**
     * @brief プールに使い終わったパーティクルを返却する。
     *
     * Pooled でないエンティティが渡された場合は即破棄。
     */
    void ReturnParticleToPool(Core::World& world, EntityID id)
    {
        using namespace CCL::ECS;
        auto* pooled = world.GetComponent<PooledParticleComponent>(id);
        if (!pooled) {
            world.Destroy(id);
            return;
        }
        pooled->isSleeping = true;

        if (auto* t = world.GetComponent<TransformComponent>(id)) {
            t->position = DirectX::XMFLOAT3(0, -9999, 0);  // 画面外に逃がす
            TransformUpdateSystem::SetParent(world, id, 0);
        }
        if (auto* p = world.GetComponent<GPUParticleComponent>(id)) {
            p->config.time = 0.0f;
        }
        internal::GetState().pool.Return(pooled->poolKey, id);
    }

    /**
     * @brief パーティクルプレハブから 1 発生成する。
     *
     * 内部処理:
     *   1. 設計図 (Blueprint) を取得 (キャッシュ済みなら即返却)
     *   2. ノードごとに、プール再利用 or 新規生成
     *   3. 寿命タイマーを設定
     *
     * @return 最後に生成/復活した EntityID。複合パーティクルの場合は最後のノード。
     */
    EntityID SpawnParticle(Core::World& world,
        const std::string& path,
        EntityID parentID,
        const DirectX::XMFLOAT3& localPos)
    {
        const auto& blueprint = internal::GetOrLoadBlueprint(path);
        if (blueprint.nodes.empty()) return 0;

        EntityID lastID = 0;
        auto& pool = internal::GetState().pool;

        for (size_t i = 0; i < blueprint.nodes.size(); ++i) {
            const auto& node = blueprint.nodes[i];
            std::string poolKey = path + "#" + std::to_string(i);

            EntityID particleID = pool.Acquire(poolKey);
            if (particleID != 0) {
                internal::WakeParticle(world, particleID, node, parentID, localPos);
            }
            else {
                particleID = internal::CreateParticle(world, node, poolKey, parentID, localPos);
            }

            internal::SetupLifetimeTimer(world, particleID, node, parentID);
            lastID = particleID;
        }
        return lastID;
    }

    // =================================================================
    // 汎用 SpawnPrefab (SceneSerializer を使う)
    // =================================================================

    /**
     * @brief JSON プレハブをロードし、ルートエンティティの ID を返す。
     *
     * 1 ファイル中に複数エンティティがある場合、parent == 0 のものを「ルート」と判断する。
     * 見つからない場合は配列先頭をルートとみなす。
     */
    EntityID SpawnPrefab(Core::World& world, const std::string& prefabPath)
    {
        // 拡張子補完
        std::string fullPath = prefabPath;
        if (fullPath.find(".json") == std::string::npos) fullPath += ".json";

        // 存在チェック (アセット欠落は実機で見つけにくい不具合の上位なので必ず警告)
        if (!FileSystem::Exists(fullPath)) {
            OutputDebugStringA(("[Prefab] Not Found: " + fullPath + "\n").c_str());
            return 0;
        }

        std::vector<EntityID> created = SceneSerializer::Deserialize(&world, fullPath);
        if (created.empty()) return 0;

        // ルート探し: parent == 0 のエンティティを優先
        EntityID rootID = created[0];
        for (EntityID id : created) {
            auto* t = world.GetComponent<TransformComponent>(id);
            if (t && t->parent == 0) { rootID = id; break; }
        }
        return rootID;
    }

    EntityID SpawnPrefab(Core::World& world, const std::string& path,
        const DirectX::XMFLOAT3& position)
    {
        EntityID id = SpawnPrefab(world, path);
        if (id != CCL::ECS::InvalidEntityID) {
            world.PatchComponent<TransformComponent>(id,
                [position](TransformComponent& t) { t.position = position; });
        }
        return id;
    }

    EntityID SpawnPrefab(Core::World& world, const std::string& path,
        const DirectX::XMFLOAT3& position,
        const DirectX::XMFLOAT4& rotation)
    {
        EntityID id = SpawnPrefab(world, path);
        if (id != CCL::ECS::InvalidEntityID) {
            world.PatchComponent<TransformComponent>(id,
                [position, rotation](TransformComponent& t) {
                    t.position = position;
                    t.rotation = rotation;
                });
        }
        return id;
    }

    EntityID SpawnPrefab(Core::World& world, const std::string& path,
        const DirectX::XMFLOAT3& position, float rotationY)
    {
        EntityID id = SpawnPrefab(world, path);
        if (id != CCL::ECS::InvalidEntityID) {
            world.PatchComponent<TransformComponent>(id,
                [position, rotationY](TransformComponent& t) {
                    t.position = position;
                    DirectX::XMVECTOR q = DirectX::XMQuaternionRotationRollPitchYaw(
                        0.0f, DirectX::XMConvertToRadians(rotationY), 0.0f);
                    DirectX::XMStoreFloat4(&t.rotation, q);
                });
        }
        return id;
    }

} // namespace Prefab