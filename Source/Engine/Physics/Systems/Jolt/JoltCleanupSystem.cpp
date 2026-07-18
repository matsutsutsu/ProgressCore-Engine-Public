// --- JoltCleanupSystem.cpp ---
#include "JoltCleanupSystem.h"
#include "Engine/Physics/JoltIntegration/JoltPhysicsManager.h"
#include "Engine/ECS/Core/CCL_World.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

void JoltCleanupSystem::Update(float dt) {
    ExecuteCleanup(*_world);
}

// 直接チャンクを回して確実に剛体を消去する関数
void JoltCleanupSystem::ExecuteCleanup(CCL::ECS::World& world)
{
    bool hasJolt = world.HasResource<JoltPhysicsManager*>();
    JoltPhysicsManager* joltManager = hasJolt ? world.GetResource<JoltPhysicsManager*>() : nullptr;
    if (!joltManager) return;

    JPH::BodyInterface& bodyInterface = joltManager->GetBodyInterface();

    auto& chunks = world.GetChunkManager().GetChunks();
    for (auto& chunkPtr : chunks) {
        if (!chunkPtr) continue;
        auto* chunk = chunkPtr.get();

        // Jolt剛体を持っていて、かつDestroyTagがついているものを狙い撃ちする
        if (!chunk->HasComponent<Tag::DestroyTag>() || !chunk->HasComponent<JoltHandleComponent>()) continue;

        size_t count = chunk->GetEntityCount();

        // 破棄した証拠を書き込むため、const を外す
        auto* joltHandles = chunk->GetComponentArray<JoltHandleComponent>();

        for (size_t i = 0; i < count; ++i) {
            if (chunk->IsEntityDestroyed(i)) continue;

            JPH::BodyID& bodyID = joltHandles[i].bodyID;

            // ★ 防波堤1: すでに破棄済み（無効なID）、または未初期化なら絶対にJoltへ渡さない
            if (bodyID.IsInvalid()) continue;

            // ★ 防波堤2: Jolt空間に「追加(Add)」されている場合のみRemoveする
            // (CreateはしたがAddしていない剛体をRemoveすると別のアサートが鳴るため)
            if (bodyInterface.IsAdded(bodyID)) {
                bodyInterface.RemoveBody(bodyID);
            }

            // 剛体の完全消去
            bodyInterface.DestroyBody(bodyID);

            // ★ 究極の修正: Joltから消し去った証拠として、コンポーネント側のIDを無効化する！
            // これにより、もしこのエンティティが次フレームまで生き残ってループに入っても、
            // 防波堤1で安全に弾かれるようになります。
            bodyID = JPH::BodyID(); // デフォルトコンストラクタは無効なID(cInvalidBodyID)を生成する
        }
    }
}

// HierarchyCleanupSystem.cpp の末尾
REGISTER_LOGIC_SYSTEM(JoltCleanupSystem, Priority::LogicStage::L07_Cleanup);