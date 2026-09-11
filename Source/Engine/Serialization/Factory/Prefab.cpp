// ===========================================================================
// Prefab.cpp
// ----------
// 役割:
//   プレハブ(永続化されたEntityテンプレート)のスポーン処理。
//
// 責務:
//   - JSONからのEntity階層生成と、座標/回転の初期適用。
//
// 設計方針:
//   - 生成のみを担い、破棄には関与しない。破棄は DestroyTag を起点に
//     HierarchyCleanupSystem(L07) が階層ごと回収する。
// ===========================================================================
#include "Prefab.h"
#include "Engine/Serialization/Serializer/SceneSerializer.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"

#include "Engine/Graphics/Shader/Shared/ShaderShared.h"

#include <DirectXMath.h>
#include <SimpleMath.h>

#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <json.hpp>
#include <tracy/Tracy.hpp>

using namespace DirectX::SimpleMath;

namespace Prefab {

    using namespace CCL::ECS;

    // =====================================================================
    // ライフサイクル
    // =====================================================================
    void Initialize(ID3D11Device* /*device*/)
    {
        // 保持するグローバル状態は無い。生成した Entity の寿命は World が握る。
        // 将来デバイス依存の事前生成が必要になったらここで行う。
    }

    // =====================================================================
    // プレハブ JSON のキャッシュ
    // =====================================================================
    //
    // なぜ必要か:
    //   VFX は「撃つたびに」プレハブから1エンティティを生成する
    //   （HitImpactEvent → HitEffectSystem → VFX::Play → SpawnPrefab）。
    //   キャッシュが無いと、ヒットのたびに VFX_HitSpark.json(24KB) を
    //   読み直して nlohmann でパースし直すことになる。
    //   ディスク読みは2回目以降 OS のページキャッシュに乗って軽くなるが、
    //   パースは純粋な CPU コストなので、何度撃っても安くならない。
    //   （Tracy 実測で1発あたり数ms。連撃でそのまま積み上がっていた）
    //
    // 何をキャッシュするか:
    //   検証済みの entities 配列そのもの。2回目以降は DeserializeRaw へ直行する。
    //
    // いつ捨てるか:
    //   エディタ構成では、エディタからプレハブを保存し直したら即座に反映されないと
    //   作業にならないので、更新時刻を見て変わっていれば読み直す。
    //   stat 1回(数μs)と 24KB のパース(数ms)ではコストが3桁違うので、
    //   毎回確認しても十分に見合う。Shipping では確認自体を行わない。
    namespace
    {
        struct CachedPrefab
        {
            nlohmann::json                 entities;  // 検証済みの entities 配列
            std::filesystem::file_time_type writeTime{}; // エディタでの編集検知用
        };

        // SpawnPrefab はイベント購読経由でも呼ばれる（＝呼び出しスレッドを限定できない）。
        // 表そのものが壊れることだけは防いでおく。
        std::mutex                                    g_prefabCacheMutex;
        std::unordered_map<std::string, CachedPrefab> g_prefabCache;

        // 現在の更新時刻を取得する。取れなければ既定値（＝比較で必ず不一致になる）。
        std::filesystem::file_time_type CurrentWriteTime(const std::string& path)
        {
            std::error_code ec;
            auto t = std::filesystem::last_write_time(path, ec);
            return ec ? std::filesystem::file_time_type{} : t;
        }
    } // namespace

    void ClearPrefabCache()
    {
        std::lock_guard<std::mutex> lock(g_prefabCacheMutex);
        g_prefabCache.clear();
    }

    void Cleanup()
    {
        // Initialize と対。生成した Entity の寿命は World が握るので解放不要だが、
        // プレハブ JSON のキャッシュだけはここで捨てる（シーンをまたいで持ち越さない）。
        ClearPrefabCache();
    }

    // =====================================================================
    // 汎用 SpawnPrefab (SceneSerializer を使う)
    // =====================================================================
    EntityID SpawnPrefab(World& world, const std::string& prefabPath)
    {
        ZoneScopedN("Prefab::SpawnPrefab");

        std::string fullPath = prefabPath;
        if (fullPath.find(".json") == std::string::npos) fullPath += ".json";

        ZoneText(fullPath.c_str(), fullPath.size());

        // --- パース済み JSON をキャッシュから引く（無ければ読んで載せる） ---
        //
        // ロックは DeserializeRaw まで通しで保持する。
        //   キャッシュ要素への参照を握ったまま抜けると、その間に別スレッドが
        //   （エディタでの更新検知で）erase した瞬間にダングリングになる。
        //   そもそも DeserializeRaw は World を書き換えるので並行実行できない。
        //   ここを直列化しても失うものは無く、危険だけが消える。
        std::lock_guard<std::mutex> lock(g_prefabCacheMutex);

        auto it = g_prefabCache.find(fullPath);

#ifdef ENABLE_EDITOR
        // エディタで保存し直された場合に取りこぼさないよう更新時刻を見る。
        // Shipping ではアセットが変わらないので、この stat 自体を行わない。
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

        EntityID rootID = created[0];
        for (EntityID id : created) {
            auto* t = world.GetComponent<TransformComponent>(id);
            if (t && t->parent == 0) { rootID = id; break; }
        }
        return rootID;
    }

    EntityID SpawnPrefab(World& world, const std::string& path,
        const DirectX::XMFLOAT3& position)
    {
        EntityID id = SpawnPrefab(world, path);
        if (id != CCL::ECS::InvalidEntityID) {
            world.PatchComponent<TransformComponent>(id,
                [position](TransformComponent& t) { t.position = position; });
        }
        return id;
    }

    EntityID SpawnPrefab(World& world, const std::string& path,
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

    EntityID SpawnPrefab(World& world, const std::string& path,
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