#include "Prefab.h"
#include "Engine/Serialization/Serializer/SceneSerializer.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"

#include <DirectXMath.h>
#include <SimpleMath.h>

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
    // プレハブ JSON のキャッシュ
    //   VFX はヒットのたびにプレハブから Entity を生成する
    //   （HitImpactEvent → HitEffectSystem → VFX::Play → SpawnPrefab）。
    //   キャッシュが無いと毎回 VFX_HitSpark.json（24KB）をパースし直し、
    //   1 発あたり数 ms かかる（Tracy で計測）。ディスク読みは OS のキャッシュで軽くなるが、
    //   パースは CPU の処理なので毎回かかる。
    //   キャッシュするのは検証済みの entities 配列で、2 回目以降は DeserializeRaw へ直接渡す。
    //   エディタ構成では、保存し直したプレハブがすぐ反映されるよう更新時刻を毎回確認する
    //   （stat は数 μs でパースより 3 桁軽い）。Shipping では確認しない。
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
    } // namespace

    void ClearPrefabCache()
    {
        std::lock_guard<std::mutex> lock(g_prefabCacheMutex);
        g_prefabCache.clear();
    }

    // ===========================================================================
    // 生成
    // ===========================================================================
    EntityID SpawnPrefab(World &world, const std::string &prefabPath)
    {
        ZoneScopedN("Prefab::SpawnPrefab");

        std::string fullPath = prefabPath;
        if (fullPath.find(".json") == std::string::npos) fullPath += ".json";

        ZoneText(fullPath.c_str(), fullPath.size());

        // パース済みの JSON をキャッシュから引く（無ければ読んで載せる）。
        //   ロックは DeserializeRaw まで保持する。要素への参照を持ったままロックを外すと、
        //   その間に別スレッドが（エディタの更新検知で）erase して参照が無効になる。
        //   DeserializeRaw は World を書き換えるのでもともと並行には走らせられず、直列にしても失うものは無い
        std::lock_guard<std::mutex> lock(g_prefabCacheMutex);

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
                return InvalidEntityID;
            }

            CachedPrefab entry;
            if (!SceneSerializer::LoadEntitiesJson(fullPath, entry.entities))
                return InvalidEntityID;
            entry.writeTime = CurrentWriteTime(fullPath);

            it = g_prefabCache.emplace(fullPath, std::move(entry)).first;
        }

        std::vector<EntityID> created = SceneSerializer::DeserializeRaw(&world, it->second.entities);
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