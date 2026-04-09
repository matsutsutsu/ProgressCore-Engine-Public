#include "NavMeshDebugDrawSystem.h"
#include "NavigationData.h"
#include <DetourNavMesh.h>
#include <DirectXMath.h>
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"
#include "Engine/Graphics/Renderer/ShapeRenderer.h"

void NavMeshDebugDrawSystem::Update(float dt)
{
    // 自身の描画フラグをチェック
    if (!isDebugVisible) return;

    // レンダラーとNavDataの取得
    if (!_world->HasResource<ShapeRenderer*>()) return;
    auto* renderer = _world->GetResource<ShapeRenderer*>();
    if (!renderer) return;

    // Resourceが登録されていない、または描画OFFなら何もしない
    if (!_world->HasResource<NavigationData>()) return;

    auto& navData = _world->GetResource<NavigationData>();
    if (!navData.navMesh || !navData.showDebugDraw) return;

    const dtNavMesh* navMesh = navData.navMesh;


    // NavMeshの全タイルを走査
    for (int i = 0; i < navMesh->getMaxTiles(); ++i) {
        const dtMeshTile* tile = navMesh->getTile(i);
        if (!tile || !tile->header) continue;

        // タイル内の全ポリゴンを走査
        for (int j = 0; j < tile->header->polyCount; ++j) {
            const dtPoly* p = &tile->polys[j];

            // 今回は歩行可能な通常のポリゴンのみ描画
            if (p->getType() == DT_POLYTYPE_OFFMESH_CONNECTION) continue;

            const dtPolyDetail* pd = &tile->detailMeshes[j];

            // ポリゴンを構成する「詳細三角形」を描画
            for (int k = 0; k < pd->triCount; ++k) {
                const unsigned char* t = &tile->detailTris[(pd->triBase + k) * 4];

                DirectX::XMFLOAT3 v[3];
                for (int m = 0; m < 3; ++m) {
                    if (t[m] < p->vertCount) {
                        float* pos = &tile->verts[p->verts[t[m]] * 3];
                        v[m] = { pos[0], pos[1], pos[2] };
                    }
                    else {
                        float* pos = &tile->detailVerts[(pd->vertBase + t[m] - p->vertCount) * 3];
                        v[m] = { pos[0], pos[1], pos[2] };
                    }
                }

                // ★注意: ここの引数はあなたの ShapeRenderer::DrawTriangle の仕様に合わせてください。
                // 仮で DirectX::XMFLOAT4 (R, G, B, Alpha) として記述しています。青色半透明。
                DirectX::XMFLOAT4 color = { 0.0f, 0.5f, 1.0f, 0.5f };

                renderer->DrawTriangle(v[0], v[1], v[2], color);
            }
        }
    }
}



REGISTER_LOGIC_SYSTEM(NavMeshDebugDrawSystem, Priority::LogicStage::L02_Update);