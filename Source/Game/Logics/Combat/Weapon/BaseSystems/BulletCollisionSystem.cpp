#include "Game/Logics/Combat/Weapon/BaseSystems/BulletCollisionSystem.h"
#include "Game/Logics/Combat/HealthComponent.h" // 敵のHP
#include "Game/Logics/Combat/Weapon/Core/BulletContextComponent.h"
 
#include "Engine/GamePlay/Physics/Collision/SphereColliderComponent.h"
#include "Engine/GamePlay/Physics/RigidBody/RigidBodyComponent.h"
#include "Engine/GamePlay/Utils/TimerComponent.h"
#include "Game/Utils/BulletPoolManager.h"

#include "Game/Logics/Combat/GlobalDamageQueue.h"

// システムの実行順序の定義ヘッダー
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"


// Randomが自作エンジンにない場合は、標準ライブラリで代用します
#include <random>

using namespace CCL::ECS;
using namespace DirectX; // XMFLOAT3やVector3を使うため

// 簡易的な乱数ヘルパー (自作エンジンのRandomがあるならそちらを使ってください)
static float GetRandomFloat01()
{
    static std::random_device                    rd;
    static std::mt19937                          gen(rd());
    static std::uniform_real_distribution<float> dis(0.0f, 1.0f);
    return dis(gen);
}

// ---------------------------------------------------------------------------
// OnHitフェーズの処理
// ---------------------------------------------------------------------------
void BulletCollisionSystem::Update(float dt)
{
    // 衝突している弾丸をすべて処理
    ForEachWithID([&](EntityID                 bulletID,
                      BulletContextComponent  &ctx,
                      RigidBodyComponent      &rb,
                      SphereColliderComponent &col,
                      BulletHitStateComponent &hitState)       
        {
            if (!col.isColliding) return;

            // 既に当たった相手なら無視 (多段ヒット防止)
            if (ctx.hitHistory.Contains(col.lastHitEntity)) return;

            // =======================================================================
            // 1. イベントフラグの設定 (OnHitシステム群へ伝える情報)
            // =======================================================================
            hitState.isHitThisFrame = true;
            hitState.targetID       = col.lastHitEntity;
            hitState.hitNormal      = col.lastHitNormal;
            hitState.hitPosition    = col.lastHitPos;


            // =======================================================================
            // 2. クリティカル判定とダメージ計算 (ゲームのコアルール)
            // =======================================================================
            bool  isCritical  = false;
            float finalDamage = ctx.currentDamage;

            if (ctx.blueprint && GetRandomFloat01() < ctx.blueprint->criticalChance) {
                isCritical = true;
                finalDamage *= ctx.blueprint->criticalMultiplier;
            }
            ctx.isCritical = isCritical;

            // =======================================================================
            // 3. 敵へのダメージ適応
            // =======================================================================
            
            // 郵便局が存在しなければ、ここで建てる（安全対策）
            if (!_world->HasResource<GlobalDamageQueue>()) {
                _world->AddResource<GlobalDamageQueue>();
            }

            // 郵便局を取得
            auto &damageQueue = _world->GetResource<GlobalDamageQueue>();

            // ノックバックベクトルの計算
            float    knockbackPower = ctx.blueprint ? ctx.blueprint->knockbackPower : 0.0f;
            XMVECTOR velocityVec    = XMVector3Normalize(XMLoadFloat3(&rb.velocity));
            XMFLOAT3 finalKnockback;
            XMStoreFloat3(&finalKnockback, velocityVec * knockbackPower);

            // ★相手がコンポーネントを持っていようがいまいが関係ない！
            // ★ただ「コイツにダメージを与えてくれ！」と郵便局に手紙を叩き込むだけ！
            damageQueue.events.push_back({
                col.lastHitEntity, // ターゲット（敵）
                finalDamage,       // ダメージ量
                finalKnockback,    // ノックバック
                bulletID,          // 攻撃者
                "BulletCollisionSystem"
            });


            // =======================================================================
            // 4. 貫通処理による生死判定 (システムが死ぬかどうかの基本ルール)
            // =======================================================================

            // 貫通の有無に関わらず、当たった相手は「絶対に」履歴に入れる！
            // これにより、弾が消滅するまでの数フレーム間に発生する多段ヒットを完全に防ぐ。
            ctx.hitHistory.Add(col.lastHitEntity);

            if (ctx.pierceCount > 0) {
                ctx.pierceCount--;
                hitState.isFatal = false;              // 貫通力が残っているため「死なない」と設定
               
            }
            else {
                hitState.isFatal = true; // 貫通力が尽きたので「死ぬ」設定（デフォルト）
            }
        });
}

REGISTER_LOGIC_SYSTEM(BulletCollisionSystem, Priority::LogicStage::Collision);