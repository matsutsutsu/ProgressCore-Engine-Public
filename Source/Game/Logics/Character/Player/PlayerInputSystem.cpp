#include "PlayerInputSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Input/IInputAPI.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/Core/Math/ScreenToWorld.h"

// ※ 実装ファイル内でのみ展開する
using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

PlayerInputSystem::PlayerInputSystem()
    : IfSystem("PlayerInputSystem")
{
}

void PlayerInputSystem::Update(float dt)
{
    // 1. ループ外での入力デバイスの一括取得 (キャッシュ効率の最大化)
    if (!_world->HasResource<std::shared_ptr<IInputAPI>>()) return;
    if (!_world->HasResource<Camera*>()) return;

    auto inputAPI = _world->GetResource<std::shared_ptr<IInputAPI>>();
    Camera* mainCamera = _world->GetResource<Camera*>();

    // Axis / Action から入力を抽出 (JSONでバインド済みと想定)
    float moveX = inputAPI->GetAxis("MoveX");
    float moveY = inputAPI->GetAxis("MoveY"); // WASDの上下
    bool isAttacking = inputAPI->GetAction("Attack");

    Vector3 moveDir(moveX, 0.0f, moveY);
    if (moveDir.LengthSquared() > 0.001f) {
        moveDir.Normalize();
    }

    // 1. 動的な画面サイズとマウス座標の取得
    float mouseX = inputAPI->GetMouseX();
    float mouseY = inputAPI->GetMouseY();
    float screenWidth = inputAPI->GetScreenWidth();
    float screenHeight = inputAPI->GetScreenHeight();

    // 2. ユーティリティを使ってワールド座標を算出
    Vector3 mouseWorldPos = ScreenToWorld::GetRayIntersectionY0(
        mouseX, mouseY, screenWidth, screenHeight, *mainCamera
    );

    // 2. エンティティ群への意思の書き込み (SoAによる超高速ループ)
    ForEach([&](CharacterMovementInputComponent& inputData, const PlayerComponent& player) {

        // プレイヤーの速度ステータスを掛け合わせ、最終的な要求速度としてペダルにセット
        inputData.desiredVelocity = moveDir * player.moveSpeed;

        // 攻撃とエイム先の更新
        inputData.isAttackRequested = isAttacking;
        inputData.lookTargetPos = mouseWorldPos;
        });
}


// ステージ L01: 全ての物理・移動処理の「前」に入力を確定させる
REGISTER_LOGIC_SYSTEM(PlayerInputSystem, Priority::LogicStage::L01_Input);