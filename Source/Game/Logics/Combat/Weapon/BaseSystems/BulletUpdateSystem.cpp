#include "BulletUpdateSystem.h"

 
#include "Game/Logics/Combat/Weapon/Core/BulletEnums.h"
#include "Engine/Physics/Grid.h"
#include "Engine/Physics/RaycastUtil.h"

#include "Engine/GamePlay/Transform/TransformUpdateSystem.h"
#include "Game/Utils/BulletPoolManager.h"
#include "Game/Utils/PooledBulletComponent.h"

#include "Game/Logics/Combat/Weapon/Core/GunBuilder.h"

#include <SimpleMath.h>

// システムの実行順序の定義ヘッダー
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

// ===================================================================================
// 実装: BulletUpdateSystem
// 役割: 毎フレームの弾丸のロジック更新
//
// 処理順序:
//   1. 寿命チェック -> 尽きたら Endフェーズ予約
//   2. Travelエフェクト適用（BulletEffectLibrary呼び出し）
//   3. 見た目の回転更新
//   4. Endフェーズ処理（爆発など）とEntity削除
// ===================================================================================


using namespace DirectX::SimpleMath;
using namespace DirectX;

void BulletUpdateSystem::Update(float dt)
{
    ForEachWithID([&](EntityID                id,
                      BulletContextComponent &ctx,
                      RigidBodyComponent     &rb,
                      TransformComponent     &trans,
                      TimerComponent          &timer,
                      BulletHitStateComponent &hitState) 
    {

        // 既に死亡処理中ならスキップ
        if (ctx.markForEnd && ctx.endExecuted) return;

        // ---------------------------------------------------------
        // 1. 生死判定 (寿命 または 致命的な衝突)
        // ---------------------------------------------------------
        ctx.lifeTime -= dt;
        if (ctx.lifeTime <= 0.0f) {
            ctx.markForEnd = true;
        }

        if (hitState.isHitThisFrame && hitState.isFatal) {
            ctx.markForEnd = true;
        }


      // ---------------------------------------------------------
        // 2. 死亡時の継承(Inheritance)処理と破棄
        // ---------------------------------------------------------
        if (ctx.markForEnd) {
            if (!ctx.endExecuted) {
                ctx.endExecuted = true;

                const BulletBlueprint *bp        = ctx.blueprint;
                bool                   hasEndMod = false;
                for (int i = 0; i < BulletBlueprint::BLOCK_COUNT; ++i) {
                    if (bp->endCounts[i] > 0) {
                        hasEndMod = true;
                        break;
                    }
                }

                // Endフェーズに何かアタッチメントがあれば継承発射
                if (hasEndMod) {
                    GunBuilder gb;
                    gb.baseDamage   = bp->baseDamage;
                    gb.baseLifeTime = bp->baseLifeTime;

                    // ※EffectIDの最大値(Reflection=9)まで回す
                    for (int i = 1; i <= (int)EffectID::Reflection; ++i) {
                        EffectID effect = static_cast<EffectID>(i);
                        // Endに設定されているエフェクト（爆発自身など）は引き継がない
                        if (bp->GetEffectCount(BulletPhase::End, effect) > 0) continue;

                        // Travel -> Travel 維持
                        int travelLevel = bp->GetEffectCount(BulletPhase::Travel, effect);
                        if (travelLevel > 0) {
                            gb.travelMods.push_back({effect, travelLevel});
                        }

                        // OnHit -> OnHit 維持
                        int hitLevel = bp->GetEffectCount(BulletPhase::OnHit, effect);
                        if (hitLevel > 0) {
                            gb.onHitMods.push_back({effect, hitLevel});
                        }
                    }

                    // 進行方向の算出
                    XMFLOAT3 dir    = {0, 0, 1};
                    XMVECTOR velVec = XMLoadFloat3(&rb.velocity);
                    if (XMVectorGetX(XMVector3LengthSq(velVec)) > 0.001f) {
                        XMStoreFloat3(&dir, XMVector3Normalize(velVec));
                    }

                    // 臨時GunBuilderを使って次世代の弾を再帰的にプールから生成！
                    gb.Fire(_world, trans.position, dir);
                }
            }

            // プールへ返却してフレーム終了
            if (_world->HasResource<BulletPoolManager>()) {
                _world->GetResource<BulletPoolManager>().ReturnToPool(*_world, id);
            }
            else {
                _world->Destroy(id);
            }
            return;
        }


        // ---------------------------------------------------------
        // 4. HitStateのクリーンアップ (次のフレームのため)
        // ---------------------------------------------------------
        hitState.Reset();

    });
}

REGISTER_LOGIC_SYSTEM(BulletUpdateSystem, Priority::LogicStage::Update);