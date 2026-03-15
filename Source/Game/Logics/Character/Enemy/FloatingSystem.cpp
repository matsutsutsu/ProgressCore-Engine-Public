#include "FloatingSystem.h"

// 実装に必要なヘッダ
#include <SimpleMath.h>

// システムの実行順序の定義ヘッダー
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

FloatingSystem::FloatingSystem() : IfSystem("FloatingSystem") {}

void FloatingSystem::Update(float dt)
{
    static float time = 0.0f;
    time += dt;

    ForEach([dt](TransformComponent &t, RigidBodyComponent &rb, const FloatingComponent &f) {
        // 1. Y座標の計算 (center.y + サイン波)
        // 3軸データですが、シンプルに安定させるため Y成分(高さ) だけを使います
        float bobbing = std::sin(time * f.speed.y + f.timeOffset) * f.amplitude.y;
        float targetY = f.center.y + bobbing;

        // 2. 座標の強制上書き
        // XとZには触らず、Yだけを支配します
        t.position.y = targetY;

        // 3. 安定化処理 (Y軸の物理挙動を抹殺)
        rb.velocity.y = 0.0f; // 落下速度をリセット
    });
}

// ==========================================
// マクロは必ず .cpp の末尾に1回だけ書く
// ==========================================
REGISTER_LOGIC_SYSTEM(FloatingSystem, Priority::LogicStage::Update);