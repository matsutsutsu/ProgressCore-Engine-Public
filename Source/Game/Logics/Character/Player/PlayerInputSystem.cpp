#include "PlayerInputSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Input/IInputAPI.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

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

    auto inputAPI = _world->GetResource<std::shared_ptr<IInputAPI>>();

    // Axis / Action から入力を抽出 (JSONでバインド済みと想定)
    float moveX = inputAPI->GetAxis("MoveX");
    float moveY = inputAPI->GetAxis("MoveY"); // WASDの上下
    bool isAttacking = inputAPI->GetAction("Attack");

    Vector3 moveDir(moveX, 0.0f, moveY);
    if (moveDir.LengthSquared() > 0.001f) {
        moveDir.Normalize();
    }

    // マウスのワールド座標を取得（レイキャスト）
    Vector3 mouseWorldPos = GetMouseWorldPositionIntersection();

    // 2. エンティティ群への意思の書き込み (SoAによる超高速ループ)
    ForEach([&](CharacterMovementInputComponent& inputData, const PlayerComponent& player) {

        // プレイヤーの速度ステータスを掛け合わせ、最終的な要求速度としてペダルにセット
        inputData.desiredVelocity = moveDir * player.moveSpeed;

        // 攻撃とエイム先の更新
        inputData.isAttackRequested = isAttacking;
        inputData.lookTargetPos = mouseWorldPos;
        });
}

Vector3 PlayerInputSystem::GetMouseWorldPositionIntersection()
{
    // ※ 注意: カメラのView/Proj行列から逆変換してY=0平面との交点を出す数学的処理がここに必要。
    // 現状はコンパイルを通すためのモックを返す。
    return Vector3::Zero;
}

// ステージ L01: 全ての物理・移動処理の「前」に入力を確定させる
REGISTER_LOGIC_SYSTEM(PlayerInputSystem, Priority::LogicStage::L01_Input);