#pragma once
#include "Engine/ECS/Core/CCL_World.h"

// モデルクラスのパスは適宜合わせてください
#include <d3d11.h>
#include <memory>

// SceneInitializer.h
class SceneInitializer {
  public:
    // ライトなどの最初のシーン設定
    static void SpawnLightStartSet(CCL::ECS::World &world, ID3D11Device *device);

    // ゲームシーン用のライト設定

    // カメラテスト
    static void SpawnCameraTest(CCL::ECS::World &world, ID3D11Device *device, int count);
    

    // ジョルトのテスト環境

	static void SpawnAnimationTest(CCL::ECS::World& world, ID3D11Device* device);


	static void SpawnGameTest(CCL::ECS::World& world, ID3D11Device* device);

	static void SpawnShaderTest(CCL::ECS::World& world, ID3D11Device* device);


    // =======================================================
    //  指定した数のモデルをグリッド状に並べるテスト
    // =======================================================
    static void SpawnModelGridTest(CCL::ECS::World& world, ID3D11Device* device, int count);



    // 「ECS限界テスト（ギャラクシー・スウォーム）」環境
    static void SpawnECSBenchmarkTest(CCL::ECS::World& world, ID3D11Device* device, int count);

    // 「幾何学の海」ベンチマークテスト
    static void SpawnVoxelOceanTest(CCL::ECS::World& world, ID3D11Device* device, int gridSize);

    // 物理エンジンの極限テスト（数万個の雪崩）
    static void SpawnPhysicsAvalancheTest(CCL::ECS::World& world, ID3D11Device* device, int count);

    // =======================================================
    // 物理エンジンの極限テスト（数万個の雪崩）
    // =======================================================
    static void SpawnJoltAvalancheTest(CCL::ECS::World &world, ID3D11Device *device, int count);

    // 究極の群れシミュレーション
    static void SpawnBoidsTest(CCL::ECS::World &world, ID3D11Device *device, int count);

    // 既存の宣言の下に追加
    static void SpawnLissajousTest(CCL::ECS::World& world, ID3D11Device* device, int count);
    static void SpawnTornadoTest(CCL::ECS::World& world, ID3D11Device* device, int count);
    static void SpawnVectorFlowTest(CCL::ECS::World& world, ID3D11Device* device, int count);

};