#include "EnemyTrackingSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include <DirectXMath.h>
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

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

    // ==============================================================
    // 2. 全エネミーの Transform を直接書き換えて移動・回転させる
    // ==============================================================

    // ★修正: TransformComponent に & (非const参照) を付け、書き換え可能にする
    ForEachWithID(
        [&](EntityID entity, TransformComponent& trans, const EnemyComponent& enemy) {

            XMVECTOR vPos = XMLoadFloat3(&trans.position);
            XMVECTOR vDir = XMVectorSubtract(vPlayerPos, vPos);

            // ★ Y軸(高さ)への追跡を無視し、地面を這うようにする
            vDir = XMVectorSetY(vDir, 0.0f);

            XMVECTOR vLength = XMVector3Length(vDir);
            float length;
            XMStoreFloat(&length, vLength);

            // プレイヤーと重ならないよう手前で停止（1.0f は適宜調整してください）
            if (length > 1.0f) {
                // 方向ベクトルを正規化（長さ1にする）
                vDir = XMVector3Normalize(vDir);

                // ----------------------------------------------------
                // [移動処理] 現在位置 + (方向 × スピード × デルタタイム)
                // ----------------------------------------------------
                XMVECTOR vMove = XMVectorScale(vDir, enemy.moveSpeed * dt);
                XMVECTOR vNewPos = XMVectorAdd(vPos, vMove);

                // Transformの座標を直接上書き
                XMStoreFloat3(&trans.position, vNewPos);

                // ----------------------------------------------------
                // [回転処理] プレイヤーの方向を向かせる（Y軸回転）
                // ----------------------------------------------------
                XMFLOAT3 dirFloat;
                XMStoreFloat3(&dirFloat, vDir);

                // Z軸を前方とした時の、X軸との角度を atan2 で計算
                float angleY = atan2f(dirFloat.x, dirFloat.z);

                // Y軸(Upベクトル)を軸にしたクォータニオン(回転値)を作成
                XMVECTOR vUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
                XMVECTOR vQuat = XMQuaternionRotationAxis(vUp, angleY);

                // Transformの回転を上書き
                XMStoreFloat4(&trans.rotation, vQuat);
            }
        }
    );
}


//REGISTER_LOGIC_SYSTEM(EnemyTrackingSystem, Priority::LogicStage::L02_Update)