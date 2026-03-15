#include "BulletRaycastSystem.h"
#include "Engine/GamePlay/Graphics/Core/StaticMeshComponent.h"
  // ComputeReflectionのため
#include "Game/Logics/Combat/Weapon/Core/BulletEnums.h"
#include "Engine/Physics/Grid.h"
#include "Engine/Physics/RaycastUtil.h"

#include "Game/Logics/World/StageMap.h"
#include "Game/Utils/BulletPoolManager.h"
#include "Game/Utils/PooledBulletComponent.h"

#include "Game/Logics/Combat/Weapon/Attachments/BulletHitStateComponent.h"

// システムの実行順序の定義ヘッダー
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

// ===================================================================================
// 実装: BulletRaycastSystem
// 役割: 弾丸のレイキャストによる壁判定と反射処理
//
// 処理ロジック:
//   1. 「反射」アタッチメントを持ち、かつ動いている弾のみを対象とする（最適化）
//   2. このフレームで進む距離分だけレイキャストを行う
//   3. 壁にヒットした場合：
//      - 衝突点まで位置を強制移動（すり抜け防止）
//      - 速度ベクトルを反射ベクトルに書き換える（物理挙動の変更）
// ===================================================================================

using namespace CCL::ECS;
using namespace DirectX;
using namespace DirectX::SimpleMath;

void BulletRaycastSystem::Update(float dt) {
    if (!_world) return;

    // リソース取得（なければ安全にリターン）
    const StageMap *map = _world->HasResource<StageMap>() ? &_world->GetResource<StageMap>() : nullptr;
    BulletPoolManager *poolMgr = _world->HasResource<BulletPoolManager>() ? &_world->GetResource<BulletPoolManager>() : nullptr;

    if (!map) return;

    ForEachWithID([&](EntityID                id,
                      const BulletContextComponent &ctx,
                      const RigidBodyComponent      &rb,
                      TransformComponent     &trans) {

        // 既に他のシステムで死んでいれば無視
        if (ctx.markForEnd) return;

        // 早期リターン
        if (auto *p = _world->GetComponent<PooledBulletComponent>(id)) {
            if (!p->isActive) return;
        }

        Vector3 currentVelocity = rb.velocity;
        float   speed           = currentVelocity.Length();
        if (speed < 0.0001f) return; // 止まっている弾は処理しない

        // 先読み移動量
        float   moveDist = speed * dt;
        Vector3 dir      = currentVelocity / speed;
        Vector3 start    = trans.position;

        // -----------------------------------------------------
        // A. 地形（グリッド）との判定
        // -----------------------------------------------------
        MapRaycastHit mapHit;
        if (map) {
            mapHit = map->Raycast(start, dir, moveDist);
        }

        // -----------------------------------------------------
        // D. 衝突結果の処理
        // -----------------------------------------------------
        // 2. 壁に当たった場合の処理
        if (mapHit.isHit) {

            // ===================================================================
            // A. システムとしての基本ルール (絶対不変)
            // ===================================================================
            // 1. 壁へのめり込みを防ぐため、衝突点へ位置を補正する
            // 少しだけ(0.01f)壁の手前に戻す
            trans.position = mapHit.point + mapHit.normal * 0.01f;



            // ===================================================================
            // C. 最終生死判定
            // ===================================================================
         
            // ★修正: 死亡や反射の処理を消し、フラグを立てるだけ
            if (auto *hitState = _world->GetComponent<BulletHitStateComponent>(id)) {
                hitState->isHitThisFrame = true;
                hitState->targetID       = CCL::ECS::InvalidEntityID; // 壁なのでInvalid
                hitState->hitNormal      = mapHit.normal;
                hitState->hitPosition    = mapHit.point;
                hitState->isFatal        = true; // デフォルトは壁に当たれば消滅
            }


            //if (hasReflection) {
            //    // --- 反射処理 ---

            //    // 速度ベクトルを反射させる ( V' = V - 2(V・N)N )
            //    Vector3 reflectedVel = Vector3::Reflect(currentVelocity, mapHit.normal);
            //    rb.velocity          = reflectedVel;

            //    // 弾の向き(Rotation)も進行方向に合わせる
            //    if (reflectedVel.LengthSquared() > 0.001f) {
            //        Vector3 forward = reflectedVel;
            //        forward.Normalize();

            //        Vector3 up = Vector3::Up;
            //        if (abs(forward.Dot(up)) > 0.99f) up = Vector3::Forward; // ジンバルロック対策

            //        Matrix lookAt  = Matrix::CreateWorld(Vector3::Zero, forward, up);
            //        trans.rotation = Quaternion::CreateFromRotationMatrix(lookAt);
            //    }
            //} else {
            //    // --- 消滅処理 ---
            //    // 反射しない弾は壁に吸われて消える
            //    // ★修正: 弾の破棄 -> プールへ返却
            //    if (poolMgr) {
            //        poolMgr->ReturnToPool(*_world, id);
            //    }
            //    else {
            //        _world->Destroy(id); // マネージャがなければ通常削除
            //    }

            //    // TODO: ここで壁ヒットエフェクト（火花など）をSpawnすると良い
            //    // EffectHitContext hitCtx { ctx, rb };
            //    // BulletEffectLibrary::ApplyOnHit(EffectID::WallHit, hitCtx);
            //}
        }
    });
}

REGISTER_LOGIC_SYSTEM(BulletRaycastSystem, Priority::LogicStage::Collision);