#pragma once
// ===========================================================================
// 概要: 角度の差を -180〜180 度（-π〜π）に収める関数
//
// 備考: 収めないと、向き直るときに1周近く遠回りする
// ===========================================================================
#include <DirectXMath.h>

namespace CCL::Math
{

    // 角度（ラジアン）を -π〜π に収める
    inline float WrapRadians(float angle)
    {
        while (angle > DirectX::XM_PI) angle -= DirectX::XM_2PI;
        while (angle < -DirectX::XM_PI) angle += DirectX::XM_2PI;
        return angle;
    }

    // 角度（度）を -180〜180 に収める
    inline float WrapDegrees(float angle)
    {
        while (angle > 180.0f) angle -= 360.0f;
        while (angle < -180.0f) angle += 360.0f;
        return angle;
    }

}
