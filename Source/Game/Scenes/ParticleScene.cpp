#include "ParticleScene.h"
#include "Engine/Scene/SceneFactory.h" // REGISTER_SCENE（ファイル末尾で自己登録）
#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Game/Logics/Test/SceneInitializer.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"


void ParticleScene::Initialize()
{
    BaseScene::Initialize();



    ID3D11Device *device = Graphics::Instance().GetDevice();
    SceneInitializer::SpawnLightStartSet(*m_worldPtr, device);

    Prefab::SpawnPrefab(*m_worldPtr, "Assets/Prefabs/Camera/FreeCamera");



    // ParticleScene固有: 特定のパーティクルを生成
    Prefab::SpawnPrefab(*m_worldPtr, "Assets/Prefabs/Particles/VFX_Scene_Start");
}

void ParticleScene::Finalize()
{
    BaseScene::Finalize(); 
}

void ParticleScene::RegisterSystems()
{
    // EditorSceneと全く同じ (SystemGroupsのおかげで短縮)
    ID3D11Device *device = Graphics::Instance().GetDevice();

    // SystemManagerへのアクセスを m_ecsContext 経由に変更
    // (SystemGroupsの関数が参照を受け取る仕様であると推測されるため、ポインタをデリファレンスしています)
    auto &sysManager = *(m_ecsContext->GetSystemManager());
    CCL::ECS::SystemRegistry::Instance().RegisterAll(sysManager);
}

// SceneFactory への自動登録（SceneManager が本クラスを include せずに済むようにする）
REGISTER_SCENE(ParticleScene, "Particle")
