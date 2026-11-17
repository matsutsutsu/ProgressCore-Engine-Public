#pragma once
// ===========================================================================
// 概要: ボスの動きの計算で、技をまたいで使う向き・位置の関数
//
// 備考: BossActionSystem の各ファイル（移動・跳躍・突進）から使う
//       ステージは平らな円（BattleContext の arenaCenter・arenaRadius）とみなす
// ===========================================================================
#include <algorithm>
#include <cmath>

#include <SimpleMath.h>

#include "Engine/Physics/Components/Character/JoltCharacterHandleComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/AI/Battle/BattleContext.h"

namespace BossActionGeometry
{
    // ステージの壁から離す距離 [m]。カプセル半径（1.4m）に余裕を足した値
    constexpr float kWallMargin = 3.0f;



    // from から to への水平の向き（正規化しない）
    inline DirectX::SimpleMath::Vector3 FlatToward(const DirectX::SimpleMath::Vector3 &from, const DirectX::SimpleMath::Vector3 &to)
    {
        DirectX::SimpleMath::Vector3 flatOffset = to - from;
        flatOffset.y                            = 0.0f;
        return flatOffset;
    }



    // 水平の前向きに対する右向き
    inline DirectX::SimpleMath::Vector3 RightOf(const DirectX::SimpleMath::Vector3 &forwardDir)
    {
        return DirectX::SimpleMath::Vector3(forwardDir.z, 0.0f, -forwardDir.x);
    }



    // lookDir の方へ、turnSpeed の速さで向き直る
    // 近い回り方で回す。lookDir がほぼ 0 なら回さない
    inline void FaceToward(TransformComponent &transform, const DirectX::SimpleMath::Vector3 &lookDir, float turnSpeed, float dt,
                           const JoltCharacterHandleComponent &joltHandle)
    {
        if (lookDir.LengthSquared() <= 0.001f) return;

        const DirectX::SimpleMath::Vector3 currentForward =
            DirectX::SimpleMath::Vector3::Transform(DirectX::SimpleMath::Vector3::UnitZ, transform.rotation);
        const float targetYaw  = atan2f(lookDir.x, lookDir.z);
        const float currentYaw = atan2f(currentForward.x, currentForward.z);

        // 差を -π〜π に収める
        float yawDiff = targetYaw - currentYaw;
        while (yawDiff > DirectX::XM_PI) yawDiff -= DirectX::XM_2PI;
        while (yawDiff < -DirectX::XM_PI) yawDiff += DirectX::XM_2PI;

        const float newYaw = currentYaw + yawDiff * (std::min)(turnSpeed * dt, 1.0f);
        SetCharacterRotation(transform, joltHandle,
                             DirectX::SimpleMath::Quaternion::CreateFromAxisAngle(DirectX::SimpleMath::Vector3::UnitY, newYaw));
    }



    // 点がステージの内側の円（壁から kWallMargin 離した円）の外なら、縁まで引き戻す。高さは変えない
    inline DirectX::SimpleMath::Vector3 ClampInsideArena(const DirectX::SimpleMath::Vector3 &point, const BattleContext &battle)
    {
        const DirectX::SimpleMath::Vector3 arenaCenterPos(battle.arenaCenter);
        const DirectX::SimpleMath::Vector3 centerToPoint = FlatToward(arenaCenterPos, point);

        const float innerRadius        = (std::max)(0.0f, battle.arenaRadius - kWallMargin);
        const float distanceFromCenter = centerToPoint.Length();
        if (distanceFromCenter <= innerRadius) return point;

        DirectX::SimpleMath::Vector3 clampedPos = arenaCenterPos + centerToPoint * (innerRadius / distanceFromCenter);
        clampedPos.y                            = point.y;
        return clampedPos;
    }



    // moveDir の向きへまっすぐ moveDistance 進んだ地点。壁の手前で止まるよう、距離だけ縮める
    // moveDir は水平で長さ 1
    inline DirectX::SimpleMath::Vector3 StraightPointInArena(const DirectX::SimpleMath::Vector3 &startPos,
                                                             const DirectX::SimpleMath::Vector3 &moveDir, float moveDistance,
                                                             const BattleContext &battle)
    {
        const DirectX::SimpleMath::Vector3 centerToStart = FlatToward(DirectX::SimpleMath::Vector3(battle.arenaCenter), startPos);
        const float                        innerRadius   = (std::max)(0.0f, battle.arenaRadius - kWallMargin);

        // 内側の円の縁に着くまでの距離（円と直線の交点）。縁の外から外向きに進むときは 0
        const float startAlongMove = centerToStart.Dot(moveDir);
        const float discriminant   = startAlongMove * startAlongMove - (centerToStart.LengthSquared() - innerRadius * innerRadius);
        float       distanceToEdge = 0.0f;
        if (discriminant > 0.0f) distanceToEdge = (std::max)(0.0f, -startAlongMove + std::sqrt(discriminant));

        return startPos + moveDir * (std::min)((std::max)(0.0f, moveDistance), distanceToEdge);
    }



    // moveDir の向きへ、壁の手前までに実際に動ける距離（最大 moveDistance）
    inline float MovableDistance(const DirectX::SimpleMath::Vector3 &startPos, const DirectX::SimpleMath::Vector3 &moveDir,
                                 float moveDistance, const BattleContext &battle)
    {
        return FlatToward(startPos, StraightPointInArena(startPos, moveDir, moveDistance, battle)).Length();
    }
}
