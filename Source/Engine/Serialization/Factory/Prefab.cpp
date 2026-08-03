// ===========================================================================
// Prefab.cpp
// ----------
// 役割:
//   プレハブ(永続化されたEntityテンプレート)のスポーン処理。
//   公開APIは2系統:
//     SpawnPrefab   : 汎用(SceneSerializer経由でフルロード)
//     SpawnParticle : パーティクル専用(ParticlePoolで階層ごと借りて高速再利用)
//
// 責務:
//   - JSONからのEntity階層生成と、座標/回転の初期適用。
//   - 複数エミッタ構成のパーティクル階層を「emitterRootごと」プールから貸し出し/返却する。
//
// アーキテクチャ上の位置付け:
//   プールの空きリストは World Resource(ParticlePool)。SpawnParticle が借り、
//   弾などの子として SetParent で接ぐ。返却は HierarchyCleanupSystem(L07) が
//   死亡Entityの子に対して ReturnParticleToPool を呼ぶ形で行う。
//
// 設計方針:
//   - パーティクルは「複数エミッタ階層」を1単位としてプールする(1弾1エミッタに制限しない)。
//   - 借りる/返すは Entity を破棄せず isSleeping のオン/オフと親子付け替えだけで行い、
//     高頻度生滅(最大8192発の弾)でも構造変更・JSONロードを最小化する。
//   - SpawnPrefab(JSONロード)はプール拡張時(空きが尽きた初回)だけ発生する。
// ===========================================================================
#include "Prefab.h"
#include "Engine/Serialization/Serializer/SceneSerializer.h"
#include "Engine/Graphics/Components/Particle/ParticlePool.h"
#include "Engine/Transform/Systems/TransformUpdateSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Graphics/Components/Particle/GPUParticleComponent.h"
#include "Engine/Graphics/Components/Particle/PooledParticleComponent.h"
#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"

#include "Engine/Graphics/Shader/Shared/ShaderShared.h"

#include <DirectXMath.h>
#include <SimpleMath.h>

#include <vector>
#include <unordered_set>

using namespace DirectX::SimpleMath;

namespace Prefab {

    using namespace CCL::ECS;

    // =====================================================================
    // 内部ヘルパー
    // =====================================================================
    namespace {

        /// @brief ParticlePool(World Resource)を取得。未登録なら遅延生成する。
        ParticlePool& GetOrCreatePool(World& world)
        {
            if (!world.HasResource<ParticlePool>())
                world.AddResource<ParticlePool>();
            return world.GetResource<ParticlePool>();
        }

        /// @brief root を起点に階層(root含む)の全EntityIDを集める(読み取りのみ)。
        /// @note  親子/兄弟リンクが破損して循環していても、訪問済み集合と
        ///        深さ上限で無限ループ/スタックオーバーフローを防ぐ。
        void CollectSubtreeImpl(World& world, EntityID root,
            std::vector<EntityID>& out,
            std::unordered_set<EntityID>& visited, int depth)
        {
            if (depth > 4096) return;                 // 異常な深さ(スタック保険)
            if (!visited.insert(root).second) return; // 既訪問 = 循環 → 打ち切り

            out.push_back(root);
            auto* t = world.GetComponent<TransformComponent>(root);
            if (!t) return;

            EntityID child = t->firstChildID;
            while (child != 0) {
                if (visited.count(child)) break;      // 兄弟リンクの循環を打ち切り
                auto* ct = world.GetComponent<TransformComponent>(child);
                if (!ct) break;
                EntityID next = ct->nextSiblingID; // 再帰前に次兄弟を退避
                CollectSubtreeImpl(world, child, out, visited, depth + 1);
                child = next;
            }
        }

        void CollectSubtree(World& world, EntityID root, std::vector<EntityID>& out)
        {
            std::unordered_set<EntityID> visited;
            CollectSubtreeImpl(world, root, out, visited, 0);
        }

    } // namespace

    // =====================================================================
    // ライフサイクル
    // =====================================================================
    void Initialize(ID3D11Device* /*device*/)
    {
        // 現状プールは World Resource 側が寿命を握るため、ここでの保持は不要。
        // 将来デバイス依存の事前生成が必要になったらここで行う。
    }

    void Cleanup()
    {
        // プールの実体は World Resource(ParticlePool)であり World と共に破棄される。
        // ここで個別に解放するグローバル状態は持たない(設計上の意図)。
    }

    void PrewarmParticles(World& /*world*/)
    {
        // TODO: ゲーム設定から「事前生成するVFXパスと個数」を受け取り、
        //       SpawnParticle→ReturnParticleToPool を繰り返してプールを温める。
        //       使用VFXの一覧が固まってから実装する(今は空でも破綻しない)。
    }

    // =====================================================================
    // パーティクル: プールから階層ごと借りて parent の子に接ぐ
    // =====================================================================
    EntityID SpawnParticle(World& world, const std::string& path,
        EntityID parentID, const DirectX::XMFLOAT3& localPos)
    {
        ParticlePool& pool = GetOrCreatePool(world);

        // 1) 休眠インスタンスを借りる。無ければJSONから新規生成(プール拡張)。
        EntityID root = pool.Acquire(path);
        if (root == InvalidEntityID) {
            root = SpawnPrefab(world, path); // 重い:空きが尽きた時のみ
            if (root == InvalidEntityID) {
                CCL_LOG_ERROR(LogCategory::Core, "[Prefab] SpawnParticle failed: %s", path.c_str());
                return InvalidEntityID;
            }
            // 新規生成階層の全Entityを「このプールキーの所属」として印付け。
            std::vector<EntityID> subtree;
            CollectSubtree(world, root, subtree);
            for (EntityID id : subtree) {
                if (auto* pooled = world.GetComponent<PooledParticleComponent>(id)) {
                    pooled->SetKey(path);
                }
                else
                {
                    PooledParticleComponent newComp;
                    newComp.SetKey(path);
                    world.AddComponent<PooledParticleComponent>(id, newComp); // 遅延適用
                }
            }
            // 新規追加分を確定させてから手順2・3で読む (拡張時のみの稀パスなので安価)
            world.ScrutinyAndApply();
        }

        // 2) parent(弾など)の子へ接ぐ + ローカル位置を設定。
        TransformUpdateSystem::SetParent(world, root, parentID);
        world.PatchComponent<TransformComponent>(root, [localPos](TransformComponent& t) {
            t.position = { localPos.x, localPos.y, localPos.z };
            t.isDirty = true;
            });

        // 3) 階層を起こす(休眠解除 + GPU再初期化で粒子を撒き直す)。
        std::vector<EntityID> subtree;
        CollectSubtree(world, root, subtree);
        for (EntityID id : subtree) {
            if (auto* pooled = world.GetComponent<PooledParticleComponent>(id))
                pooled->isSleeping = false;
            if (auto* gp = world.GetComponent<GPUParticleComponent>(id)) {
                gp->needsGpuInit = true; // InitCSで粒子をリセットして再放出
                gp->time = 0.0f;
            }
        }
        return root;
    }

    // =====================================================================
    // パーティクル: 階層ごとプールへ返す(破棄しない)
    //   HierarchyCleanupSystem(L07) が死亡Entityの子(emitterRoot)に対して呼ぶ。
    // =====================================================================
    void ReturnParticleToPool(World& world, EntityID rootId)
    {
        if (rootId == InvalidEntityID) return;

        auto* pooled = world.GetComponent<PooledParticleComponent>(rootId);
        if (!pooled) return; // プール対象でなければ何もしない(保険)

        // 1) 親(弾)から切り離してルート化。
        TransformUpdateSystem::DetachChild(world, rootId);

        // 2) 配下を全休眠(Update/Renderのスキップ対象になる)。
        std::vector<EntityID> subtree;
        CollectSubtree(world, rootId, subtree);
        for (EntityID id : subtree) {
            if (auto* p = world.GetComponent<PooledParticleComponent>(id))
                p->isSleeping = true;
        }

        // 3) 空きリストへ返す(Entityは破棄せず再利用待ちにする)。
        GetOrCreatePool(world).Release(pooled->poolKey, rootId);
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