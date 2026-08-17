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

#include <vector>

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

    void Cleanup()
    {
        // Initialize と対。解放すべきグローバル状態を持たない(設計上の意図)。
    }

    // =====================================================================
    // 汎用 SpawnPrefab (SceneSerializer を使う)
    // =====================================================================
    EntityID SpawnPrefab(World& world, const std::string& prefabPath)
    {
        std::string fullPath = prefabPath;
        if (fullPath.find(".json") == std::string::npos) fullPath += ".json";

        if (!FileSystem::Exists(fullPath))
        {
            CCL_LOG_ERROR(LogCategory::Core, "[Prefab] Not Found: %s", fullPath.c_str());
            return InvalidEntityID;
        }

        std::vector<EntityID> created = SceneSerializer::Deserialize(&world, fullPath);
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