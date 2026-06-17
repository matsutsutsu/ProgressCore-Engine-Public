// ===========================================================================
// KnockbackSystem.cpp
// -------------------
// 役割・責務・設計方針は KnockbackSystem.h を参照。
//
// 実装の意図(Why):
//   - 減衰は velocity *= pow(decayPerSec, dt) の指数減衰。フレームレート非依存で
//     「ドンと吹っ飛んで滑らかに止まる」game feel を出す(線形減算だと急停止する)。
//   - 稼働は isActive フラグで制御し、付け外しをしない。これにより構造変更が無く
//     各エンティティが独立に処理できるため ForEachParallel で安全に並列化できる。
//   - 時間は各自の localDt を使用。ヒットストップ中は吹っ飛びもピタリと止まる。
// ===========================================================================
#include "KnockbackSystem.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <cmath>

using namespace CCL::ECS;

namespace
{
    constexpr float kStopThresholdSq = 0.01f * 0.01f; ///< 速度がこれ未満(二乗)で停止扱い
}

void KnockbackSystem::Update(float dt)
{
    ForEachParallel([](TransformComponent& trans,
        KnockbackComponent& kb,
        const TimeStateComponent& time)
        {
            if (!kb.isActive) return; // 停止中はスキップ(構造変更なし・低コスト)

            const float ldt = time.localDt;

            // 1. 位置へ反映(XZのみ。平面ステージ)
            trans.position.x += kb.velocity.x * ldt;
            trans.position.z += kb.velocity.z * ldt;
            trans.isDirty = true;

            // 2. 指数減衰(フレームレート非依存)
            const float factor = std::pow(kb.decayPerSec, ldt);
            kb.velocity.x *= factor;
            kb.velocity.z *= factor;

            // 3. 十分小さくなったら停止(コンポーネントは外さず isActive=false に戻すだけ)
            if (kb.velocity.LengthSquared() < kStopThresholdSq)
            {
                kb.velocity = DirectX::SimpleMath::Vector3::Zero;
                kb.isActive = false;
            }
        });
}

// 通常移動と同じ L02_Update で外力として消化する。
REGISTER_LOGIC_SYSTEM(KnockbackSystem, Priority::LogicStage::L02_Update);