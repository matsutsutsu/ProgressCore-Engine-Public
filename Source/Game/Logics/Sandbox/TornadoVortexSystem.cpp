#include "TornadoVortexSystem.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

void TornadoVortexSystem::Update(float dt)
{
    ForEachParallel([&](TransformComponent& trans, TornadoVortexComponent& tornado) {

        // 円柱座標系の更新
        tornado.radius -= tornado.shrinkSpeed * dt;
        tornado.angle += tornado.rotationSpeed * dt;
        tornado.height += tornado.riseSpeed * dt;

        // リセット処理（中心付近に到達するか、上空に上がりすぎたら外縁・底面に戻す）
        if (tornado.radius <= 0.5f || tornado.height > 50.0f) {
            tornado.radius = tornado.maxRadius;
            tornado.height = 0.0f;
        }

        // 円柱座標から直交座標(XYZ)への変換
        trans.position.x = tornado.centerPos.x + std::cos(tornado.angle) * tornado.radius;
        trans.position.y = tornado.centerPos.y + tornado.height;
        trans.position.z = tornado.centerPos.z + std::sin(tornado.angle) * tornado.radius;
        });
}

// 自動登録を止めている（実験用のため本編では使わない）。
//   登録したままだと全シーンへ入り、走らないのに実行順の解決対象としてだけ残って、
//   起動時の「順序不定ペア」警告を埋めてしまう（7本で 101 件中 47 件を占めていた）。
//   使うときはこの行のコメントを外す。
// REGISTER_LOGIC_SYSTEM(TornadoVortexSystem, Priority::LogicStage::L02_Update);