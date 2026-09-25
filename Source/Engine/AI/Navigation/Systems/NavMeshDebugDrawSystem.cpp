#include "NavMeshDebugDrawSystem.h"
#include "Engine/AI/Navigation/Core/NavigationData.h"
#include <DetourNavMesh.h>
#include <DirectXMath.h>
#include <cmath>

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Graphics/Renderer/ShapeRenderer.h"

#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/AI/Navigation/Components/NavAgentComponent.h"

void NavMeshDebugDrawSystem::Update(float dt)
{
    if (!isDebugVisible) return;

    if (!m_world->HasResource<ShapeRenderer *>()) return;
    auto *renderer = m_world->GetResource<ShapeRenderer *>();
    if (!renderer) return;

    // 1. ナビメッシュ（歩ける範囲）を描く
    if (m_world->HasResource<NavigationData>())
    {
        auto &navData = m_world->GetResource<NavigationData>();
        if (navData.navMesh && navData.showDebugDraw)
        {
            const dtNavMesh *navMesh = navData.navMesh;

            for (int i = 0; i < navMesh->getMaxTiles(); ++i)
            {
                const dtMeshTile *tile = navMesh->getTile(i);
                if (!tile || !tile->header) continue;

                for (int j = 0; j < tile->header->polyCount; ++j)
                {
                    const dtPoly *p = &tile->polys[j];
                    if (p->getType() == DT_POLYTYPE_OFFMESH_CONNECTION) continue;

                    // ① ポリゴンの外周を黒い線で描く
                    for (int k = 0; k < p->vertCount; ++k)
                    {
                        float *v0 = &tile->verts[p->verts[k] * 3];
                        float *v1 = &tile->verts[p->verts[(k + 1) % p->vertCount] * 3];

                        // 地面に埋まらないよう、少しだけ上げる
                        DirectX::XMFLOAT3 p0 = {v0[0], v0[1] + 0.08f, v0[2]};
                        DirectX::XMFLOAT3 p1 = {v1[0], v1[1] + 0.08f, v1[2]};

                        renderer->DrawLine(p0, p1, {0.0f, 0.0f, 0.0f, 1.0f});
                    }

                    // ② ポリゴンの面を青い半透明で描く（詳細メッシュの三角形単位）
                    const dtPolyDetail *pd = &tile->detailMeshes[j];
                    for (int k = 0; k < pd->triCount; ++k)
                    {
                        const unsigned char *t = &tile->detailTris[(pd->triBase + k) * 4];
                        DirectX::XMFLOAT3    v[3];
                        for (int m = 0; m < 3; ++m)
                        {
                            // 番号がポリゴンの頂点数未満ならポリゴンの頂点、以上なら詳細メッシュの追加頂点を指す
                            if (t[m] < p->vertCount)
                            {
                                float *pos = &tile->verts[p->verts[t[m]] * 3];
                                // 面は線より下（+0.05）に置く
                                v[m] = {pos[0], pos[1] + 0.05f, pos[2]};
                            }
                            else
                            {
                                float *pos = &tile->detailVerts[(pd->vertBase + t[m] - p->vertCount) * 3];
                                v[m] = {pos[0], pos[1] + 0.05f, pos[2]};
                            }
                        }
                        renderer->DrawSolidTriangle(v[0], v[1], v[2], {0.0f, 0.5f, 1.0f, 0.4f});
                    }
                }
            }
        }
    }

    // 2. 各エージェントの残り経路と半径を描く
    auto view = m_world->View<TransformComponent, NavAgentComponent>();
    for (auto entity : view)
    {
        auto *transform = m_world->GetComponent<TransformComponent>(entity);
        auto *agent     = m_world->GetComponent<NavAgentComponent>(entity);

        // ① 自分の位置から、残りの waypoint を赤い線で結ぶ
        if (!agent->waypoints.empty() && agent->currentWaypointIndex < agent->waypoints.size())
        {
            DirectX::XMFLOAT3 prevPos = transform->position;
            for (size_t i = agent->currentWaypointIndex; i < agent->waypoints.size(); ++i)
            {
                DirectX::XMFLOAT3 nextPos = agent->waypoints[i];
                // 地面に埋まらないよう少し浮かせる
                prevPos.y += 0.2f;
                nextPos.y += 0.2f;
                renderer->DrawLine(prevPos, nextPos, {1.0f, 0.0f, 0.0f, 1.0f});
                prevPos = nextPos;
            }
        }

        // ② 半径を黄色い円で描く（半径は固定の目安値）
        float drawRadius = 0.6f;
        int   segments   = 16;
        for (int i = 0; i < segments; ++i)
        {
            float theta1 = (i / (float)segments) * 2.0f * 3.14159f;
            float theta2 = ((i + 1) / (float)segments) * 2.0f * 3.14159f;

            DirectX::XMFLOAT3 p1 = {
                transform->position.x + cos(theta1) * drawRadius,
                transform->position.y + 0.1f,
                transform->position.z + sin(theta1) * drawRadius};
            DirectX::XMFLOAT3 p2 = {
                transform->position.x + cos(theta2) * drawRadius,
                transform->position.y + 0.1f,
                transform->position.z + sin(theta2) * drawRadius};

            renderer->DrawLine(p1, p2, {1.0f, 1.0f, 0.0f, 1.0f});
        }
    }
}

REGISTER_RENDER_SYSTEM(NavMeshDebugDrawSystem, Priority::RenderStage::R08_Main);