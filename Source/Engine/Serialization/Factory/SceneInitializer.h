#pragma once
#include "ECS/Core/CCL_World.h"

// モデルクラスのパスは適宜合わせてください
#include <d3d11.h>
#include <memory>

// SceneInitializer.h
class SceneInitializer {
public:
    // ライトなどの最初のシーン設定
    static void SpawnLightStartSet(CCL::ECS::Core::World& world, ID3D11Device* device);

    // ゲームシーン用のライト設定
    static void SpawnGameLightSetting(CCL::ECS::Core::World& world, ID3D11Device* device);

    // マップチップのシーン設定
    static void SpawnMapChipTest(CCL::ECS::Core::World& world, ID3D11Device* device);


    // 「静止球体と弾丸」のテスト環境
    static void SpawnBasicCollisionTest(CCL::ECS::Core::World& world, ID3D11Device* device);

    // 「大量1000体ランダム移動」のストレステスト環境
    static void SpawnPhysicsStressTest(CCL::ECS::Core::World& world, ID3D11Device* device, int count);

    // Boidsの群衆シミュレーションテスト
    static void SpawnBoidsTest(CCL::ECS::Core::World& world, ID3D11Device* device, int count);

    // プレイヤー操作可能なキャラクターのテスト環境
    static void SpawnPlayerTest(CCL::ECS::Core::World& world, ID3D11Device* device, int count);

    // カメラテスト
    static void SpawnCameraTest(CCL::ECS::Core::World& world, ID3D11Device* device, int count);

    // 「大量のオブジェクトの整列配置（モデル表示のみ）」
    static void SpawnGridLayoutTest(CCL::ECS::Core::World& world, ID3D11Device* device, int count);

    // モデルを大量に描画するだけ
    static void SpawnModelRenderTest(CCL::ECS::Core::World& world, ID3D11Device* device, int count);

    // ★追加: Transform負荷テスト（マルチスレッド確認用）
    // 大量のオブジェクトを生成し、毎フレームの行列更新負荷をかけます。
    static void SpawnTransformLoadTest(
        CCL::ECS::Core::World &world, ID3D11Device *device, int count);


    // 様々な形状（Sphere, Box, Capsule）の衝突テスト
    static void SpawnCollisionShapeTest(CCL::ECS::Core::World &world, ID3D11Device *device);
};