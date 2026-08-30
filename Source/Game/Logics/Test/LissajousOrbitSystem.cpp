#include "LissajousOrbitSystem.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

void LissajousOrbitSystem::Update(float dt)
{
    // Chunkごとに並列実行される高効率なループ
    ForEachParallel([&](TransformComponent& trans, LissajousOrbitComponent& lissajous) {

        lissajous.timeAcc += dt * lissajous.speed;
        float t = lissajous.timeAcc;

        // リサージュ方程式に基づく絶対位置の計算
        trans.position.x = lissajous.centerPos.x + lissajous.amplitude.x * std::sin(lissajous.frequency.x * t + lissajous.phase.x);
        trans.position.y = lissajous.centerPos.y + lissajous.amplitude.y * std::sin(lissajous.frequency.y * t + lissajous.phase.y);
        trans.position.z = lissajous.centerPos.z + lissajous.amplitude.z * std::cos(lissajous.frequency.z * t + lissajous.phase.z);
        });
}

// Logicステージにシステムを自動登録
// 自動登録を止めている（実験用のため本編では使わない）。
//   登録したままだと全シーンへ入り、走らないのに実行順の解決対象としてだけ残って、
//   起動時の「順序不定ペア」警告を埋めてしまう（7本で 101 件中 47 件を占めていた）。
//   使うときはこの行のコメントを外す。
// REGISTER_LOGIC_SYSTEM(LissajousOrbitSystem, Priority::LogicStage::L02_Update);