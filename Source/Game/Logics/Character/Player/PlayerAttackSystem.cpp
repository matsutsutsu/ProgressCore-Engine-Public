#include "PlayerAttackSystem.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Game/Logics/LevelGen/DungeonGrid.h"
#include <algorithm>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;


void PlayerAttackSystem::Update(float /*dt*/)
{
    // グリッドが存在しないシーン（テスト等）ではシステムを回さないか、早期リターンする
    if (!_world->HasResource<RuneForge::Dungeon::DungeonGrid>()) return;

    // ループ外で一度だけリソースの参照を取得する（キャッシュ最適化）
    const auto& grid = _world->GetResource<RuneForge::Dungeon::DungeonGrid>();

    ForEach([&grid](WandFireIntentComponent& intent,
        const CharacterMovementInputComponent& input,
        const CharacterWallRadiusComponent& wall,
        const TransformComponent& xform,
        const PlayerComponent& /*player*/)
        {
            // 1. 発射トリガーの転送
            intent.wantFire = input.isAttackRequested;

            // 2. 照準方向の計算 (XZ平面のみ)
            Vector3 toTarget = input.lookTargetPos - xform.position;
            toTarget.y = 0.0f; // トップビュー: 高さ方向の差は無視

            if (toTarget.LengthSquared() > 1e-6f)
            {
                toTarget.Normalize();
                intent.aimDirection = toTarget;
            }

            // 3. 銃口位置 (Muzzle Position) の絶対座標計算と「壁抜け防止」
            float wandHeight = 1.0f;          // 発射の高さ(Y軸)

            // ★ DDAレイキャストによる壁抜け補正
            // キャラクター中心から、照準方向に向かってベース半径分だけレイを飛ばす
            auto hit = grid.RaycastDDA(
                xform.position.x,
                xform.position.z,
                intent.aimDirection.x,
                intent.aimDirection.z,
                wall.radius
            );

            float actualOffset = wall.radius;

            if (hit.hit)
            {
                // 壁に当たった場合、壁に完全に埋まらないように 0.1f (マージン) 手前に銃口を引く
                constexpr float MUZZLE_MARGIN = 0.1f;
                actualOffset = (std::max)(0.0f, hit.distance - MUZZLE_MARGIN);
            }

            // 最終的な銃口の絶対座標を確定
            intent.muzzlePosition = xform.position + (intent.aimDirection * actualOffset);
            intent.muzzlePosition.y += wandHeight;
        });
}

// L01_Inputに登録。TaskScheduler が依存関係を解析し、
// PlayerInputSystem(Write<CMIC>) の直後に自動で配置してくれる。
REGISTER_LOGIC_SYSTEM(PlayerAttackSystem, Priority::LogicStage::L01_Input);