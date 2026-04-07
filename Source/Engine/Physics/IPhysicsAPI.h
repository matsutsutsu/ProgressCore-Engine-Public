#pragma once
#include "ECS/Common/CCL_Common.h"
#include <DirectXMath.h>

// 物理エンジンへの純粋なアクセス窓口（Joltの依存一切なし）
class IPhysicsAPI {
public:
    virtual ~IPhysicsAPI() = default;

    // 剛体に力を加える
    virtual void AddForce(CCL::ECS::EntityID entity, const DirectX::XMFLOAT3& force) = 0;

    // 剛体の速度を直接上書きする
    virtual void SetLinearVelocity(CCL::ECS::EntityID entity, const DirectX::XMFLOAT3& velocity) = 0;

    // 剛体の現在の速度を取得する
    virtual DirectX::XMFLOAT3 GetLinearVelocity(CCL::ECS::EntityID entity) = 0;

    // 指定した距離だけ真下にレイを飛ばし、接地しているか判定する
    virtual bool CheckGrounded(CCL::ECS::EntityID entity, float distance) = 0;

    // ※ 必要に応じて AddTorque(回転力) などを追加していく
};