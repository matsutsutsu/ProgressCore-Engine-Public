#include "EnemyTrackingSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include <DirectXMath.h>
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

// ★追加: 物理APIを使うためのインクルード
#include "Engine/Physics/JoltIntegration/IPhysicsAPI.h"
// ※ もし EngineServices などから取得する場合は適宜インクルードしてください

using namespace CCL::ECS;
using namespace DirectX;

EnemyTrackingSystem::EnemyTrackingSystem() : IfSystem("EnemyTrackingSystem") {}

void EnemyTrackingSystem::Update(float dt)
{
    // ==============================================================
    // 1. プレイヤーの座標を検索
    // ==============================================================
    XMFLOAT3 playerPos = { 0, 0, 0 };
    bool foundPlayer = false;

    // 手動クエリでPlayerComponentを持つエンティティを探す
    auto playerView = _world->View<PlayerComponent, TransformComponent>();
    for (auto entity : playerView) {
        auto* transComp = _world->GetComponent<TransformComponent>(entity);
        playerPos = transComp->position;
        foundPlayer = true;
        break;
    }

    // プレイヤーが存在しない場合は追跡しない
    if (!foundPlayer) return;

    XMVECTOR vPlayerPos = XMLoadFloat3(&playerPos);
    float moveSpeed = 100.0f; // ★ 物理用に少し速めに設定

    // ==============================================================
    // 2. 物理APIの取得 
    // ==============================================================
    // 1. Facade(IPhysicsAPI)がResourceに登録されているかチェック
    if (!_world->HasResource<std::shared_ptr<IPhysicsAPI>>()) return;

    // shared_ptrを取り出してローカルに保持
    auto physicsAPI = _world->GetResource<std::shared_ptr<IPhysicsAPI>>();

    // ==============================================================
    // 3. 全エネミーを物理エンジン経由で移動させる
    // ==============================================================
    // EntityID を受け取るパターンの ForEach を使用します。
   // --- EnemyTrackingSystem.cpp の ForEachWithID 内部を書き換え ---

    ForEachWithID(
        [&](EntityID entity, const TransformComponent& trans, const EnemyComponent& enemy) {

            XMVECTOR vPos = XMLoadFloat3(&trans.position);
            XMVECTOR vDir = XMVectorSubtract(vPlayerPos, vPos);

            // Y軸(高さ)への追跡を無視
            vDir = XMVectorSetY(vDir, 0.0f);

            XMVECTOR vLength = XMVector3Length(vDir);
            float length;
            XMStoreFloat(&length, vLength);

            if (length > 1.0f) {
                // プレイヤーの方向ベクトル（正規化）
                vDir = XMVector3Normalize(vDir);

                // 1. 現在の水平速度を取得
                XMFLOAT3 currentVel = physicsAPI->GetLinearVelocity(entity);
                XMVECTOR vCurrentVel = XMVectorSet(currentVel.x, 0.0f, currentVel.z, 0.0f);

                // 2. 目標とする理想の速度
                XMVECTOR vDesiredVel = XMVectorScale(vDir, moveSpeed);

                // 3. 理想と現実の「差分（足りない速度）」を計算
                XMVECTOR vVelocityDiff = XMVectorSubtract(vDesiredVel, vCurrentVel);

                // 4. 差分に比例した「力(Force)」を加える（ステアリング・フォース）
                // 係数(50.0fなど)が大きいほど機敏に追従し、小さいほど慣性が残って滑る
                float forceMultiplier = 500.0f;
                XMFLOAT3 force;
                XMStoreFloat3(&force, XMVectorScale(vVelocityDiff, forceMultiplier));

                // 速度を上書きせず、力を加えることで「横に広がる余地」をJoltに残す
                physicsAPI->AddForce(entity, force);

                // 向きは常にプレイヤーの方を向かせる
                XMFLOAT3 lookDir;
                XMStoreFloat3(&lookDir, vDir);
                physicsAPI->SetLookDirection(entity, lookDir);
            }
            else {
                // プレイヤーに密着した場合のブレーキ処理（目標速度をゼロにして力を加える）
                XMFLOAT3 currentVel = physicsAPI->GetLinearVelocity(entity);
                XMVECTOR vCurrentVel = XMVectorSet(currentVel.x, 0.0f, currentVel.z, 0.0f);
                XMVECTOR vDesiredVel = XMVectorZero();
                XMVECTOR vVelocityDiff = XMVectorSubtract(vDesiredVel, vCurrentVel);

                float brakeMultiplier = 50.0f;
                XMFLOAT3 force;
                XMStoreFloat3(&force, XMVectorScale(vVelocityDiff, brakeMultiplier));

                physicsAPI->AddForce(entity, force);
            }
        }
    );
}

REGISTER_LOGIC_SYSTEM(EnemyTrackingSystem,Priority::LogicStage::L02_Update)