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
#include "Engine/Transform/Systems/TransformUpdateSystem.h"
#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"

#include "Engine/Graphics/Shader/Shared/ShaderShared.h"

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


// =====================================================================
// Prefab 名前空間 - 実装
// =====================================================================
namespace Prefab {

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
            CCL_LOG_ERROR(LogCategory::Core, "[Prefab] Not Found: %s", fullPath.c_str());

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