#pragma once
#include <SimpleMath.h>

struct ProtoDroneComponent
{
    enum class State
    {
        Approach,  // プレイヤーに群がる
        DashAttack // 直線的に突撃する
    };

    State                        currentState  = State::Approach;
    float                        stateTimer    = 0.0f;
    DirectX::SimpleMath::Vector3 dashDirection = {0, 0, 0};
};