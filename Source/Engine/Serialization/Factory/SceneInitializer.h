#pragma once
#include "ECS/Core/CCL_World.h"

// モデルクラスのパスは適宜合わせてください
#include <d3d11.h>
#include <memory>

// SceneInitializer.h
class SceneInitializer {
  public:
    // ライトなどの最初のシーン設定
    static void SpawnLightStartSet(CCL::ECS::Core::World &world, ID3D11Device *device);

    // ゲームシーン用のライト設定
    static void SpawnGameLightSetting(CCL::ECS::Core::World &world, ID3D11Device *device);

    // 「大量1000体ランダム移動」のストレステスト環境
    static void SpawnPhysicsStressTest(
        CCL::ECS::Core::World &world, ID3D11Device *device, int count);

    // プレイヤー操作可能なキャラクターのテスト環境
    static void SpawnPlayerTest(CCL::ECS::Core::World &world, ID3D11Device *device, int count);

    // カメラテスト
    static void SpawnCameraTest(CCL::ECS::Core::World &world, ID3D11Device *device, int count);

    // 「大量のオブジェクトの整列配置（モデル表示のみ）」
    static void SpawnGridLayoutTest(CCL::ECS::Core::World &world, ID3D11Device *device, int count);

    // Transform負荷テスト（マルチスレッド確認用）
    // 大量のオブジェクトを生成し、毎フレームの行列更新負荷をかけます。
    static void SpawnTransformLoadTest(
        CCL::ECS::Core::World &world, ID3D11Device *device, int count);

    // プレイヤーと大量の追従敵を発生させるストレステスト
    static void SpawnEnemySwarmTest(CCL::ECS::Core::World &world, ID3D11Device *device, int count);

    // ジョルトのテスト環境
    static void SpawnJoltTest(CCL::ECS::Core::World &world, ID3D11Device *device);

	static void SpawnAnimationTest(CCL::ECS::Core::World& world, ID3D11Device* device);

	static void SpawnShaderTest(CCL::ECS::Core::World& world, ID3D11Device* device);

    // 「ECS限界テスト（ギャラクシー・スウォーム）」環境
    static void SpawnECSBenchmarkTest(CCL::ECS::Core::World& world, ID3D11Device* device, int count);

    // 「幾何学の海」ベンチマークテスト
    static void SpawnVoxelOceanTest(CCL::ECS::Core::World& world, ID3D11Device* device, int gridSize);

    // 物理エンジンの極限テスト（数万個の雪崩）
    static void SpawnPhysicsAvalancheTest(CCL::ECS::Core::World& world, ID3D11Device* device, int count);

};