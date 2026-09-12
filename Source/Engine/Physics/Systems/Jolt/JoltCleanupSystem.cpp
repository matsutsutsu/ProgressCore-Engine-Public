// --- JoltCleanupSystem.cpp ---
#include "JoltCleanupSystem.h"
#include "Engine/Physics/JoltIntegration/JoltPhysicsManager.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Transform/Systems/HierarchyCleanupSystem.h" // 「誰が消えるか」の唯一の定義元

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <vector>

void JoltCleanupSystem::Update(float dt) {
    ExecuteCleanup(*m_world);
}

// 消えるEntityのJolt剛体を確実に破棄する
void JoltCleanupSystem::ExecuteCleanup(CCL::ECS::World& world)
{
    bool hasJolt = world.HasResource<JoltPhysicsManager*>();
    JoltPhysicsManager* joltManager = hasJolt ? world.GetResource<JoltPhysicsManager*>() : nullptr;
    if (!joltManager) return;

    JPH::BodyInterface& bodyInterface = joltManager->GetBodyInterface();

    // DestroyTag で絞り込んではいけない。
    //   DestroyTag は「削除の起点」にしか付かないのに、HierarchyCleanupSystem は
    //   その子孫まで破棄する。チャンクを DestroyTag で絞ると子の剛体を取りこぼし、
    //   ECS から消えた後もブロードフェーズに居座り続ける（＝見えないコライダー）。
    //   実例: Player.json / Muriel.json の武器は親を持つ子Entityで、剛体を持つ。
    //   そのため判定は HierarchyCleanupSystem と同じ収集関数に一本化する。
    std::vector<CCL::ECS::EntityID> doomed;
    doomed.reserve(64);
    HierarchyCleanupSystem::CollectDoomedEntities(world, doomed);

    for (CCL::ECS::EntityID id : doomed) {
        // 破棄済みの席なら GetComponent が nullptr を返すので、それが有効判定を兼ねる
        auto* handle = world.GetComponent<JoltHandleComponent>(id);
        if (!handle) continue;

        JPH::BodyID& bodyID = handle->bodyID;

        // 防波堤1: すでに破棄済み（無効なID）、または未初期化なら絶対にJoltへ渡さない
        if (bodyID.IsInvalid()) continue;

        // 防波堤2: Jolt空間に「追加(Add)」されている場合のみRemoveする
        // (CreateはしたがAddしていない剛体をRemoveすると別のアサートが鳴るため)
        if (bodyInterface.IsAdded(bodyID)) {
            bodyInterface.RemoveBody(bodyID);
        }

        // 剛体の完全消去
        bodyInterface.DestroyBody(bodyID);

        // Jolt から破棄したらコンポーネント側のIDも無効化する。
        // エンティティが次フレームまで残っていても、上の有効判定で弾かれ二重破棄にならない。
        bodyID = JPH::BodyID(); // デフォルトコンストラクタは無効なID(cInvalidBodyID)を生成する
    }
}

REGISTER_LOGIC_SYSTEM(JoltCleanupSystem, Priority::LogicStage::L07_Cleanup);
