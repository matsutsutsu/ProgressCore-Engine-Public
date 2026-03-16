#include "HierarchyCleanupSystem.h"
#include "Engine/GamePlay/Transform/TransformUpdateSystem.h" // DetachChildを使うため
#include "ECS/Core/CCL_World.h"
#include <algorithm> // for std::unique if needed
#include "Engine/Serialization/Factory/Prefab.h" // ★追加: ReturnParticleToPoolを使うため
#include "Game/Utils/PooledParticleComponent.h"
#include <vector>

// Jolt関係のヘッダー
#include "Engine/Physics/JoltPhysicsManager.h"
#include "Engine/GamePlay/Physics/Jolt/JoltHandleComponent.h"

// 各システムの .cpp ファイルの上部に追加
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

using namespace CCL::ECS;
using namespace CCL::ECS::Core;

HierarchyCleanupSystem::HierarchyCleanupSystem() : IfSystem("HierarchyCleanupSystem") {}

void HierarchyCleanupSystem::Update(float dt)
{
    // 削除対象を一時的に保持するリスト
    // ECSのメモリ構造を変更しながらのイテレーションは危険なため、
    // まず「誰を消すか」を確定させてから処理を実行します。
    std::vector<EntityID> executionList;
    executionList.reserve(64); // メモリ確保回数を減らすための予約

    // 1. DestroyTagがついている「削除起点」となるエンティティを見つける
    ForEachWithID([&](EntityID id, const Tag::DestroyTag &tag) {
        // まず自分自身をリストに追加
        executionList.push_back(id);

        // Transformコンポーネントを持っているなら、その子供たちも道連れにするために収集
        if (_world->HasComponent<TransformComponent>(id)) {
            CollectChildrenRecursive(id, executionList);
        }
    });

    // Joltのマネージャへのアクセスを準備
    bool hasJolt = _world->HasResource<JoltPhysicsManager>();
    JoltPhysicsManager* joltManager = hasJolt ? &_world->GetResource<JoltPhysicsManager>() : nullptr;

    // 2. 集めたエンティティを一括処理
    for (EntityID targetID : executionList) {
        // リスト作成中に重複して登録されている場合や、既に無効な場合はスキップ
        if (!_world->IsEntityValid(targetID)) continue;

        bool isPooled = _world->HasComponent<PooledParticleComponent>(targetID);
      
        // 親子関係の解除 (プールに戻す場合も、削除する場合も、リンクは切る必要がある)
        if (_world->HasComponent<TransformComponent>(targetID)) {
            TransformUpdateSystem::DetachChild(*_world, targetID);
        }

        if (isPooled) {
            Prefab::ReturnParticleToPool(*_world, targetID);
            _world->RequestRemoveComponent<Tag::DestroyTag>(targetID);

            // ※もしパーティクルにJoltBodyが付いている場合は、ここにも処理が必要になりますが、
            // 一般的にエフェクトには物理演算を付けないため今回は考慮していません。
        }
        else {
            // ==========================================================
            // ★重要追加: ECSから削除する前に、Joltの物理空間から剛体を消去する
            // ==========================================================
            if (hasJolt && _world->HasComponent<JoltHandleComponent>(targetID)) {
                auto &joltBody = *_world->GetComponent<JoltHandleComponent>(targetID);
                JPH::BodyInterface &bodyInterface = joltManager->GetBodyInterface();

                // 1. 物理シミュレーション空間から取り除く (Remove)
                bodyInterface.RemoveBody(joltBody.bodyID);
                // 2. メモリから完全に破棄する (Destroy)
                bodyInterface.DestroyBody(joltBody.bodyID);
            }

            // 通常のエンティティなら完全に削除
            _world->RequestDestroyEntity(targetID);
        }

    }
}

void HierarchyCleanupSystem::CollectChildrenRecursive(
    EntityID currentID, std::vector<EntityID> &outList)
{
    // 現在のIDのTransformを取得
    auto *trans = _world->GetComponent<TransformComponent>(currentID);
    if (!trans) return;

    // 子供リストの先頭を取得
    EntityID childID = trans->firstChildID;

    // 兄弟リンクを辿って全ての子供を走査
    while (childID != 0) {
        auto *childTrans = _world->GetComponent<TransformComponent>(childID);
        if (!childTrans) break; // 安全策

        // リストに追加
        outList.push_back(childID);

        // 次の兄弟のIDを先に保存しておく（再帰で構造が変わるわけではないが念のため）
        EntityID nextSibling = childTrans->nextSiblingID;

        // さらにその子供（孫）がいれば再帰的に収集
        if (childTrans->firstChildID != 0) {
            CollectChildrenRecursive(childID, outList);
        }

        // 次の兄弟へ進む
        childID = nextSibling;
    }
}

// HierarchyCleanupSystem.cpp の末尾
REGISTER_LOGIC_SYSTEM(HierarchyCleanupSystem, Priority::LogicStage::Cleanup);