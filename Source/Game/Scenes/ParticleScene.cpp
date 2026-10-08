#include "ParticleScene.h"
#include "Engine/Scene/SceneFactory.h"
#include "Engine/Serialization/Serializer/LevelSerializer.h"
#include "Engine/Platform/Core/Logger.h"
#include "tracy/Tracy.hpp"

#include "Engine/ECS/System/CCL_SystemRegistry.h"

void ParticleScene::Initialize()
{
    BaseScene::Initialize();

    // LoadLevel は PersistentTag の付いていないエンティティを全部消してから読み込む。
    // BaseScene::Initialize はエンティティを作らないので、消されて困るものは無い
    {
        std::string particleLevel = "Assets/Scene/ParticleScene.level.json";

        ZoneScopedN("ParticleScene: LoadLevel ParticleScene");
        if (!LevelSerializer::LoadLevel(m_worldPtr, particleLevel))
            CCL_LOG_ERROR(LogCategory::Core, "ParticleScene: パーティクルシーンレベルの読み込みに失敗: %s", particleLevel.c_str());
    }
}

void ParticleScene::Finalize()
{
    BaseScene::Finalize();
}

void ParticleScene::RegisterSystems()
{
    auto &sysManager = *(m_ecsContext->GetSystemManager());
    CCL::ECS::SystemRegistry::Instance().RegisterAll(sysManager);
}

// SceneFactory へ登録する（SceneManager がこのクラスを include せずに済むように）
REGISTER_SCENE(ParticleScene, "Particle")
