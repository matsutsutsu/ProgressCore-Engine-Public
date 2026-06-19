// ===========================================================================
// TargetRegistrySystem.cpp
// ------------------------
// 役割・責務・設計方針はヘッダを参照。
//
// 実装の意図(Why):
//   - 毎フレーム先頭でリストを作り直す。敵は移動・生滅するため、前フレームの位置を
//     使うと追尾がぶれる。1フレーム1回の再構築で常に最新を保つ。
//   - DeadTag 付き(消滅予約)の敵は登録しない。死ぬ瞬間の敵に吸い込まれるのを防ぐ。
//   - Resource未登録なら遅延生成する(明示登録に依存しない・安全側)。
// ===========================================================================
#include "TargetRegistrySystem.h"
#include "Game/Logics/Combat/TargetRegistry.h"
#include "Game/Logics/Combat/DeadTag.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

void TargetRegistrySystem::Update(float /*dt*/)
{
    // Resource を取得（無ければ遅延生成）。
    if (!_world->HasResource<TargetRegistry>())
        _world->AddResource<TargetRegistry>();
    TargetRegistry& reg = _world->GetResource<TargetRegistry>();

    // フレーム先頭でクリアし、生存中の敵を全登録する。
    reg.Clear();

    ForEachWithID([&](EntityID id,
        const EnemyComponent& /*enemy*/,
        const TransformComponent& t)
        {
            if (_world->HasComponent<Tag::DeadTag>(id)) return; // 消滅予約は除外
            reg.Add(id, t.position);
        });
}

REGISTER_LOGIC_SYSTEM(TargetRegistrySystem, Priority::LogicStage::L02_PreUpdate);