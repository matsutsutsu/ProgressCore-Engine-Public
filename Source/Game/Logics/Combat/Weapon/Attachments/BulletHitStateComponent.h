#pragma once
#include "ECS/Common/CCL_Common.h"
#include <DirectXMath.h>

// 命中判定システムからOnHitシステム群へ情報を伝えるための軽量な伝令役
struct BulletHitStateComponent {
    bool isHitThisFrame = false; // 今フレームで当たったか？
    bool isFatal        = true;  // この衝突で死ぬべきか？（反射アタッチメントがfalseにする）

    CCL::ECS::EntityID targetID    = CCL::ECS::InvalidEntityID;
    DirectX::XMFLOAT3  hitNormal   = {0.0f, 1.0f, 0.0f};
    DirectX::XMFLOAT3  hitPosition = {0.0f, 0.0f, 0.0f};

    // フレームの最後に呼ばれるシステムでリセットするための関数
    void Reset()
    {
        isHitThisFrame = false;
        isFatal        = true;
        targetID       = CCL::ECS::InvalidEntityID;
    }
};