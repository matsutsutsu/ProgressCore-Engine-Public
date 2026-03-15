#include "GroundCheckSystem.h"
#include "Engine/Graphics/Renderer/ShapeRenderer.h"
#include "Engine/Physics/RaycastUtil.h"

// 各システムの .cpp ファイルの上部に追加
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

void GroundCheckSystem::Update(float elapsedTime)
{
    using namespace CCL::ECS;

    if (!_world->HasResource<Grid>()) return;
    Grid& grid = _world->GetResource<Grid>();

    //  ShapeRendererを取得（なければ描画しないだけ）
    ShapeRenderer* debugDrawer = nullptr;
    if (_world->HasResource<ShapeRenderer*>()) {
        debugDrawer = _world->GetResource<ShapeRenderer*>();
    }

    // このForEachの中に処理を書くことで対応したチャンク内のEntity１つの処理を動かせる
    ForEach([&](TransformComponent            &trans,
                RigidBodyComponent            &bodies,
                const CollisionConfigComponent &configs) {
        if (!configs.checkGround) return;
        // 足元から少し上から下へレイを飛ばす
        Vector3 rayOrigin = Vector3(trans.position) + Vector3(0, configs.groundRayOffset, 0);
        Vector3 rayDir = Vector3(0, -1, 0);
        Ray ray(rayOrigin, rayDir);

        RaycastHit hit;

        // 判定実行
        bool isHit = RaycastUtil::ClosestHit(*_world, grid, ray, configs.groundRayLength, hit);


        if (isHit) {

            // 上向きの速度が一定以上（ジャンプ中）なら、地面補正をスキップする
            if (bodies.velocity.y > 0.1f) {
                bodies.isGrounded = false; // 上昇中なので接地はしていない
                return;
            }

            // ここは「下向きに移動中」または「静止中」のときだけ通る
            bodies.isGrounded = true;
            // 地面に設置させる
            trans.position.y = hit.position.y + 0.01f;
            if (bodies.velocity.y < 0) bodies.velocity.y = 0;


            // 判定成功時のデバッグ描画（計算結果 hit をそのまま使う！）
            if (this->isDebugVisible && debugDrawer) {
                debugDrawer->DrawLine(rayOrigin, hit.position, { 0, 1, 0, 1 }); // 緑
                debugDrawer->DrawSphere(hit.position, 0.05f, { 0, 1, 0, 1 });
            }
        }
        else {
            bodies.isGrounded = false;

            // 判定失敗時のデバッグ描画
            if (this->isDebugVisible && debugDrawer) {
                debugDrawer->DrawRay(rayOrigin, rayDir, configs.groundRayLength, { 1, 0, 0, 1 }); // 赤
            }
        }

        });
}

REGISTER_LOGIC_SYSTEM(GroundCheckSystem, Priority::LogicStage::Collision);