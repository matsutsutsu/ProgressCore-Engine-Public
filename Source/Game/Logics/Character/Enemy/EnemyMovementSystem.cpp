///// @file  EnemyMovementSystem.cpp
///// @brief MoveIntent に従い、NavGridFlowField（距離場）で壁を避けて移動・回転する。
/////
///// 移動方向の決め方:
/////   1. 距離場が有効なら、自分のセルの4近傍で「距離が最小のセル」へ向かう（壁回避ナビ）
/////   2. 距離場が無い/到達不可/最終接近時は、desiredTarget へ直線（フォールバック）
/////   壁スライド付き（壁に入る場合は X / Z 軸を個別に試す）。
/////
///// ★ StateTree（AI）はこの仕組みを一切知らない。AI は MoveIntent を書くだけ。
//#include "EnemyMovementSystem.h"
//
//#include "Game/Logics/LevelGen/DungeonGrid.h"        // パスは適宜合わせてください
//#include "Game/Logics/AI/FlowField/NavGridFlowField.h"
//#include "Engine/ECS/Core/CCL_World.h"
//#include "Engine/ECS/System/CCL_SystemRegistry.h"
//#include "Engine/Core/Systems/SystemPriority.h"
//
//#include <DirectXMath.h>
//
//using namespace CCL::ECS;
//using namespace DirectX;
//using namespace RuneForge::Dungeon;
//
//EnemyMovementSystem::EnemyMovementSystem() : IfSystem("EnemyMovementSystem") {}
//
//void EnemyMovementSystem::Update(float dt)
//{
//    // Resource はフレーム頭で1回だけ取得（ForEach 内で毎回引かない）
//    DungeonGrid* grid = _world->HasResource<DungeonGrid>()
//        ? &_world->GetResource<DungeonGrid>() : nullptr;
//    NavGridFlowField* field = _world->HasResource<NavGridFlowField>()
//        ? &_world->GetResource<NavGridFlowField>() : nullptr;
//
//    ForEach(
//        [&](TransformComponent& trans, 
//            const MoveIntentComponent& intent,
//            const TimeStateComponent& time) // ★ 個別時間を受け取る
//        {
//            if (!intent.hasTarget) return;
//
//            const XMVECTOR vPos = XMLoadFloat3(&trans.position);
//            XMFLOAT3 moveDirF = { 0, 0, 0 };
//            bool haveDir = false;
//
//            // ----------------------------------------------------
//            // 1. 距離場ナビ：最小 dist の隣接セルへ
//            // ----------------------------------------------------
//            if (grid && field && field->valid) {
//                int ei, ej;
//                grid->WorldToCell(trans.position.x, trans.position.z, ei, ej);
//                const int curDist = field->Dist(ei, ej);
//
//                if (curDist > 0) {   // curDist==0 はプレイヤーと同セル → フォールバックで直線
//                    int bestI = ei, bestJ = ej, best = curDist;
//                    const int di[4] = { 1, -1, 0, 0 };
//                    const int dj[4] = { 0, 0, 1, -1 };
//                    for (int k = 0; k < 4; ++k) {
//                        const int ni = ei + di[k];
//                        const int nj = ej + dj[k];
//                        const int nd = field->Dist(ni, nj);
//                        if (nd >= 0 && nd < best) { best = nd; bestI = ni; bestJ = nj; }
//                    }
//
//                    if (bestI != ei || bestJ != ej) {
//                        const float tx = grid->CellCenterWorldX(bestI);
//                        const float tz = grid->CellCenterWorldZ(bestJ);
//                        XMVECTOR vd = XMVectorSet(tx - trans.position.x, 0.0f,
//                            tz - trans.position.z, 0.0f);
//                        float len = 0.0f; XMStoreFloat(&len, XMVector3Length(vd));
//                        if (len > 1e-4f) {
//                            XMStoreFloat3(&moveDirF, XMVector3Normalize(vd));
//                            haveDir = true;
//                        }
//                    }
//                }
//            }
//
//            // ----------------------------------------------------
//            // 2. フォールバック：desiredTarget へ直線（グリッド無し or 最終接近）
//            // ----------------------------------------------------
//            if (!haveDir) {
//                XMVECTOR vT = XMLoadFloat3(&intent.desiredTarget);
//                XMVECTOR vd = XMVectorSubtract(vT, vPos);
//                vd = XMVectorSetY(vd, 0.0f);
//                float len = 0.0f; XMStoreFloat(&len, XMVector3Length(vd));
//                if (len > 1e-4f) {
//                    XMStoreFloat3(&moveDirF, XMVector3Normalize(vd));
//                    haveDir = true;
//                }
//            }
//
//            if (!haveDir) return;
//            const XMVECTOR vDir = XMLoadFloat3(&moveDirF);
//
//            // ----------------------------------------------------
//            // 3. 移動（speed>0 のときだけ）＋壁スライド
//            // ----------------------------------------------------
//            if (intent.desiredSpeed > 0.0f) {
//                XMVECTOR vNew = XMVectorAdd(vPos, XMVectorScale(vDir, intent.desiredSpeed * time.localDt));
//                XMFLOAT3 np; XMStoreFloat3(&np, vNew);
//
//                if (grid && grid->IsWallAtWorld(np.x, np.z)) {
//                    // 壁に入る → X だけ / Z だけ動かして滑らせる
//                    if (!grid->IsWallAtWorld(np.x, trans.position.z)) {
//                        np.z = trans.position.z;             // X方向のみ
//                    }
//                    else if (!grid->IsWallAtWorld(trans.position.x, np.z)) {
//                        np.x = trans.position.x;             // Z方向のみ
//                    }
//                    else {
//                        np = trans.position;                 // 詰み → 停止
//                    }
//                }
//                trans.position = np;
//            }
//
//            // ----------------------------------------------------
//            // 4. 回転：移動中は進行方向、攻撃中(speed=0)はプレイヤーを向く
//            // ----------------------------------------------------
//            XMFLOAT3 faceF = moveDirF;
//            if (intent.desiredSpeed <= 0.0f) {
//                XMVECTOR vd = XMVectorSubtract(XMLoadFloat3(&intent.desiredTarget), vPos);
//                vd = XMVectorSetY(vd, 0.0f);
//                float len = 0.0f; XMStoreFloat(&len, XMVector3Length(vd));
//                if (len > 1e-4f) XMStoreFloat3(&faceF, XMVector3Normalize(vd));
//            }
//
//            const float angleY = atan2f(faceF.x, faceF.z);
//            const XMVECTOR vQuat = XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), angleY);
//            XMStoreFloat4(&trans.rotation, vQuat);
//        });
//}
//
//REGISTER_LOGIC_SYSTEM(EnemyMovementSystem, Priority::LogicStage::L02_Update)