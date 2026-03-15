#pragma once
#include "Collision.h"
#include "ECS/Common/CCL_Common.h"
#include "ECS/Core/CCL_Chunk.h"
#include "ECS/Core/CCL_World.h"
#include "Engine/GamePlay/Graphics/Core/ModelComponent.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Grid.h"
#include <SimpleMath.h>

// ===================================================================================
// クラス名: RaycastUtil
// 役割: Gridを使用した高速レイキャスト判定を提供するユーティリティクラス
//
// 機能:
//   - ClosestHit: レイを飛ばして、最も手前にあるオブジェクトを検出する
//   - Gridのセルをレイに沿って走査し、Collision.h の詳細判定を呼び出す
// ===================================================================================

// Ray構造体はSimpleMathのものを使う
using namespace DirectX::SimpleMath;

struct RaycastHit {
    CCL::ECS::EntityID entity = 0;
    Vector3            position;
    Vector3            normal;
    float              distance = FLT_MAX;
};

// SimpleMathのRay構造体：
// 開始地点をoriginではなくpositionという名前で持っている

class RaycastUtil {
  public:
    static bool ClosestHit(CCL::ECS::Core::World &world,
        const Grid                               &grid,
        const Ray                                &ray,
        float                                     maxDistance,
        RaycastHit                               &outHit)
    {
        bool hitAnything = false;
        outHit.distance  = maxDistance;

        Vector3 rayEnd = ray.position + ray.direction * maxDistance;
        float   cs     = grid.cellSize;

        // 判定用ラムダ式
        auto checkEntity = [&](CCL::ECS::EntityID id) {
            if (!world.IsEntityValid(id)) return;

            auto *trans     = world.GetComponent<TransformComponent>(id);
            auto *modelComp = world.GetComponent<ModelComponent>(id);

            if (!trans || !modelComp || !modelComp->modelPtr) return;

            // 3. モデル取得
            const Model *model = modelComp->GetModel();

            // ここでも強力なチェックを行う
            // (IsBadPtrと同じロジック、またはIsBadPtrをヘッダで共有して使う)
            uintptr_t mAddr = reinterpret_cast<uintptr_t>(model);
            if (mAddr == 0 || mAddr == 0xFFFFFFFFFFFFFFFF || mAddr == 0xDDDDDDDDDDDDDDDD ||
                mAddr == 0xCDCDCDCDCDCDCDCD) {
                return;
            }

            Matrix worldMat = Matrix::CreateScale(trans->scale) *
                              Matrix::CreateFromQuaternion(trans->rotation) *
                              Matrix::CreateTranslation(trans->position);

            DirectX::XMFLOAT3 hitPos, hitNormal;
            if (Collision::RayCast(ray.position, rayEnd, worldMat, model, hitPos, hitNormal)) {
                float d = Vector3::Distance(ray.position, hitPos);
                if (d < outHit.distance) {
                    outHit.distance = d;
                    outHit.position = hitPos;
                    outHit.normal   = hitNormal;
                    outHit.entity   = id;
                    hitAnything     = true;
                }
            }
        };

        // === 1. 静的メッシュチェック ===
        for (auto &id : grid.staticLargeMeshes) {
            checkEntity(id);
        }

        // === 2. 動的グリッドチェック ===
        // レイの開始地点周辺のセルをチェック（元のロジックを維持）
        // dx: -1~1, dy: 0~-2, dz: -1~1
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dy = 0; dy >= -2; --dy) {

                    Vector3 searchPos = ray.position + Vector3(dx * cs, dy * cs, dz * cs);

                    grid.ForEachInCell(searchPos,
                        [&](CCL::ECS::EntityID id, ColliderType type) { checkEntity(id); });
                }
            }
        }

        return hitAnything;
    }
};