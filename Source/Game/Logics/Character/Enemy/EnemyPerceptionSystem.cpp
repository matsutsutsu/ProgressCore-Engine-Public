///// @file  EnemyPerceptionSystem.cpp
///// @brief EnemyPerceptionSystem の実装。
//#include "EnemyPerceptionSystem.h"
//#include "Engine/ECS/Core/CCL_World.h"
//#include "Engine/ECS/System/CCL_SystemRegistry.h"
//#include "Engine/Core/Systems/SystemPriority.h"
//#include "../Player/PlayerComponent.h"   // パスは適宜合わせてください
//
//#include <DirectXMath.h>
//
//using namespace CCL::ECS;
//using namespace DirectX;
//
//EnemyPerceptionSystem::EnemyPerceptionSystem() : IfSystem("EnemyPerceptionSystem") {}
//
//void EnemyPerceptionSystem::Update(float /*dt*/)
//{
//    // ==============================================================
//    // 1. プレイヤーを1回だけ検索（旧 EnemyTrackingSystem と同じ手法）
//    // ==============================================================
//    XMFLOAT3 playerPos = { 0, 0, 0 };
//    EntityID playerEntity = InvalidEntityID;
//
//    auto playerView = _world->View<PlayerComponent, TransformComponent>();
//    for (auto entity : playerView) {
//        auto* t = _world->GetComponent<TransformComponent>(entity);
//        playerPos = t->position;
//        playerEntity = entity;
//        break;
//    }
//
//    const bool hasPlayer = (playerEntity != InvalidEntityID);
//    const XMVECTOR vPlayerPos = XMLoadFloat3(&playerPos);
//
//    // ==============================================================
//    // 2. 全敵の Perception を更新（距離・方向は XZ 平面で計算）
//    // ==============================================================
//    ForEachWithIDParallel(
//        [&](EntityID /*entity*/,
//            const TransformComponent& trans,
//            const EnemyComponent&     /*enemy*/,
//            PerceptionComponent& perc)
//        {
//            if (!hasPlayer) {
//                perc.target = InvalidEntityID;
//                perc.distance = FLT_MAX;
//                return;
//            }
//
//            XMVECTOR vPos = XMLoadFloat3(&trans.position);
//            XMVECTOR vDir = XMVectorSubtract(vPlayerPos, vPos);
//            vDir = XMVectorSetY(vDir, 0.0f);   // 高さ無視（地面を這う）
//
//            float length = 0.0f;
//            XMStoreFloat(&length, XMVector3Length(vDir));
//
//            perc.target = playerEntity;
//            perc.lastKnownPos = playerPos;
//            perc.distance = length;
//
//            // 方向（ゼロ距離時は前方ベクトルを維持）
//            if (length > 0.0001f) {
//                XMFLOAT3 dirF;
//                XMStoreFloat3(&dirF, XMVector3Normalize(vDir));
//                perc.dirToTarget = dirF;
//            }
//        });
//}
//
//REGISTER_LOGIC_SYSTEM(EnemyPerceptionSystem, Priority::LogicStage::L02_Update)