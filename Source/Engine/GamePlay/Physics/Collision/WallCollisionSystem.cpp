#include "WallCollisionSystem.h"
#include "Engine/Graphics/Renderer/ShapeRenderer.h"
#include "Engine/Physics/RaycastUtil.h"

// 各システムの .cpp ファイルの上部に追加
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

void WallCollisionSystem::Update(float dt)
{
	using namespace CCL::ECS;

	if (!_world->HasResource<Grid>()) return;
	Grid& grid = _world->GetResource<Grid>();

	//  ShapeRendererを取得（なければ描画しないだけ）
	ShapeRenderer* debugDrawer = nullptr;
	if (_world->HasResource<ShapeRenderer*>()) {
		debugDrawer = _world->GetResource<ShapeRenderer*>();
	}


	ForEach([&](TransformComponent            &trans,
                    RigidBodyComponent            &bodies,
                    const CollisionConfigComponent &configs) {
		if (!configs.checkWall) return;

		// 水平方向の速度ベクトル
		Vector3 velXZ = Vector3(bodies.velocity.x, 0, bodies.velocity.z);
		float speed = velXZ.Length();
		if (speed < 0.01f) return;

		Vector3 dir = velXZ / speed;
		Vector3 rayOrigin = Vector3(trans.position) + Vector3(0, configs.wallCheckHeight, 0);
		Ray ray(rayOrigin, dir);

		RaycastHit hit;
		// 半径(wallRayLength) + このフレームの移動量分をチェック
		float checkDist = configs.wallRayLength + (speed * dt);

		// 判定実行
		bool isHit = RaycastUtil::ClosestHit(*_world, grid, ray, checkDist, hit);


		if (isHit) {
			// 壁の法線を使って押し戻す
			float penetration = checkDist - hit.distance;
			Vector3 pushBack = hit.normal * penetration;

			trans.position.x += pushBack.x;
			trans.position.z += pushBack.z;

			// 壁沿いに滑る処理（法線方向の速度を消す）
			float dot = velXZ.Dot(hit.normal);
			if (dot < 0) {
				Vector3 slideVel = velXZ - hit.normal * dot;
				bodies.velocity.x = slideVel.x;
				bodies.velocity.z = slideVel.z;
			}

			// 判定成功時のデバッグ描画（計算結果 hit をそのまま使う！）
			if (this->isDebugVisible && debugDrawer) {
				debugDrawer->DrawLine(rayOrigin, hit.position, { 0, 1, 0, 1 }); // 緑
				debugDrawer->DrawSphere(hit.position, 0.05f, { 0, 1, 0, 1 });
			}
		}
		else
		{
			// 判定失敗時のデバッグ描画
			if (this->isDebugVisible && debugDrawer) {
				debugDrawer->DrawRay(rayOrigin, dir, checkDist, { 1, 0, 0, 1 }); // 赤
			}
		}

		});
}

REGISTER_LOGIC_SYSTEM(WallCollisionSystem, Priority::LogicStage::Collision);