#include "Prefab.h"
#include "Engine/Serialization/Serializer/SceneSerializer.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Graphics/Resource/ResourceManager.h"

#include <DirectXMath.h>
#include <SimpleMath.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <json.hpp>
#include <tracy/Tracy.hpp>

using namespace DirectX::SimpleMath;

namespace Prefab
{

    using namespace CCL::ECS;

    // ===========================================================================
    // プレハブの JSON のキャッシュ。VFX はヒットのたびにプレハブから作るので、毎回パースすると1発で数 ms かかる
    // 持つのは検証済みの entities の配列。エディタ構成では、保存し直したプレハブをすぐ反映するよう更新時刻を毎回見る
    // ===========================================================================
    namespace
    {
        struct CachedPrefab
        {
            nlohmann::json                  entities;    // 検証済みの entities 配列
            std::filesystem::file_time_type writeTime{}; // エディタでの編集検知用
        };

        // SpawnPrefab はイベントの購読からも呼ばれ、呼び出すスレッドを限定できないのでロックする
        std::mutex                                    g_prefabCacheMutex;
        std::unordered_map<std::string, CachedPrefab> g_prefabCache;

        // ファイルの更新時刻を取得する。取れなければ既定値を返す
        std::filesystem::file_time_type CurrentWriteTime(const std::string &path)
        {
            std::error_code ec;
            auto            t = std::filesystem::last_write_time(path, ec);
            return ec ? std::filesystem::file_time_type{} : t;
        }



        // 拡張子のないパスに .json を足す（呼び出し側は "Assets/Prefabs/Particles/VFX_Jump" のように書ける）
        std::string ToJsonPath(const std::string &prefabPath)
        {
            std::string fullPath = prefabPath;
            if (fullPath.find(".json") == std::string::npos) fullPath += ".json";
            return fullPath;
        }



        // キャッシュから引く。なければ読んで載せる。読めなければ nullptr
        // g_prefabCacheMutex を持ったまま呼ぶこと
        const CachedPrefab *FindOrLoadCached(const std::string &fullPath)
        {
            auto it = g_prefabCache.find(fullPath);

#ifdef ENABLE_EDITOR
            // エディタで保存し直されていたら読み直す（Shipping ではアセットが変わらないので確認しない）
            if (it != g_prefabCache.end() && CurrentWriteTime(fullPath) != it->second.writeTime)
            {
                g_prefabCache.erase(it);
                it = g_prefabCache.end();
            }
#endif

            if (it == g_prefabCache.end())
            {
                if (!FileSystem::Exists(fullPath))
                {
                    CCL_LOG_ERROR(LogCategory::Core, "[Prefab] Not Found: %s", fullPath.c_str());
                    return nullptr;
                }

                CachedPrefab entry;
                if (!SceneSerializer::LoadEntitiesJson(fullPath, entry.entities))
                {
                    return nullptr;
                }
                entry.writeTime = CurrentWriteTime(fullPath);

                it = g_prefabCache.emplace(fullPath, std::move(entry)).first;
            }
            return &it->second;
        }



        // JSON の中の文字列から、テクスチャのパス（拡張子で判定）を集める
        // コンポーネント名では探さない。表示名を変えると JSON のキーも変わり、黙って拾えなくなるため
        void CollectTexturePaths(const nlohmann::json &node, std::vector<std::string> &out)
        {
            if (node.is_string())
            {
                const std::string &s   = node.get_ref<const std::string &>();
                const size_t       dot = s.find_last_of('.');
                if (dot == std::string::npos) return;

                std::string ext = s.substr(dot);
                std::transform(ext.begin(), ext.end(), ext.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

                if (ext == ".png" || ext == ".dds" || ext == ".tga" || ext == ".jpg" || ext == ".jpeg" ||
                    ext == ".hdr")
                {
                    out.push_back(s);
                }
                return;
            }

            if (node.is_object() || node.is_array())
            {
                for (const auto &child : node)
                {
                    CollectTexturePaths(child, out);
                }
            }
        }
    } // namespace

    void ClearPrefabCache()
    {
        std::lock_guard<std::mutex> lock(g_prefabCacheMutex);
        g_prefabCache.clear();
    }



    // ===========================================================================
    // 先読み
    // ===========================================================================
    bool Preload(const std::string &prefabPath)
    {
        ZoneScopedN("Prefab::Preload");

        const std::string fullPath = ToJsonPath(prefabPath);
        ZoneText(fullPath.c_str(), fullPath.size());

        // パスだけ集めてロックを外す。テクスチャの読み込みは重く、
        // 持ったままだとその間に別スレッドの SpawnPrefab を止めてしまう
        std::vector<std::string> texturePaths;
        {
            std::lock_guard<std::mutex> lock(g_prefabCacheMutex);

            const CachedPrefab *cached = FindOrLoadCached(fullPath);
            if (!cached) return false;

            CollectTexturePaths(cached->entities, texturePaths);
        }

        // 生成時に GPUParticle などが同じパスで LoadTexture するので、ここで読めばその時はキャッシュに当たる
        auto &rm = ResourceManager::Instance();
        for (const std::string &path : texturePaths)
        {
            rm.LoadTexture(path.c_str());
        }
        return true;
    }



    // ===========================================================================
    // 生成
    // ===========================================================================
    EntityID SpawnPrefab(World &world, const std::string &prefabPath)
    {
        ZoneScopedN("Prefab::SpawnPrefab");

        const std::string fullPath = ToJsonPath(prefabPath);
        ZoneText(fullPath.c_str(), fullPath.size());

        // パース済みの JSON をキャッシュから引く（なければ読んで載せる）
        //   ロックは DeserializeRaw まで保持する。要素への参照を持ったままロックを外すと、
        //   その間に別スレッドが（エディタの更新検知で）erase して参照が無効になる
        //   DeserializeRaw は World を書き換えるのでもともと並行には走らせられず、直列にしても失うものはない
        std::lock_guard<std::mutex> lock(g_prefabCacheMutex);

        const CachedPrefab *cached = FindOrLoadCached(fullPath);
        if (!cached) return InvalidEntityID;

        std::vector<EntityID> created = SceneSerializer::DeserializeRaw(&world, cached->entities);
        if (created.empty())
            return InvalidEntityID;

        // 親を持たない Entity をルートとして返す
        EntityID rootID = created[0];
        for (EntityID id : created)
        {
            auto *t = world.GetComponent<TransformComponent>(id);
            if (t && t->parent == 0)
            {
                rootID = id;
                break;
            }
        }
        return rootID;
    }



    EntityID SpawnPrefab(World &world, const std::string &path,
                         const DirectX::XMFLOAT3 &position)
    {
        EntityID id = SpawnPrefab(world, path);
        if (id != CCL::ECS::InvalidEntityID)
        {
            world.PatchComponent<TransformComponent>(id,
                                                     [position](TransformComponent &t) { t.position = position; });
        }
        return id;
    }



    EntityID SpawnPrefab(World &world, const std::string &path,
                         const DirectX::XMFLOAT3 &position,
                         const DirectX::XMFLOAT4 &rotation)
    {
        EntityID id = SpawnPrefab(world, path);
        if (id != CCL::ECS::InvalidEntityID)
        {
            world.PatchComponent<TransformComponent>(id,
                                                     [position, rotation](TransformComponent &t) {
                                                         t.position = position;
                                                         t.rotation = rotation;
                                                     });
        }
        return id;
    }



    EntityID SpawnPrefab(World &world, const std::string &path,
                         const DirectX::XMFLOAT3 &position, float rotationY)
    {
        EntityID id = SpawnPrefab(world, path);
        if (id != CCL::ECS::InvalidEntityID)
        {
            world.PatchComponent<TransformComponent>(id,
                                                     [position, rotationY](TransformComponent &t) {
                                                         t.position          = position;
                                                         DirectX::XMVECTOR q = DirectX::XMQuaternionRotationRollPitchYaw(
                                                             0.0f, DirectX::XMConvertToRadians(rotationY), 0.0f);
                                                         DirectX::XMStoreFloat4(&t.rotation, q);
                                                     });
        }
        return id;
    }

} // namespace Prefab