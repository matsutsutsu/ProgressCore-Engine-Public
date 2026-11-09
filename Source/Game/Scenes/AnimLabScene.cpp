#include "AnimLabScene.h"
#include "Engine/Scene/SceneFactory.h"
#include "Engine/Serialization/Serializer/LevelSerializer.h"
#include "Engine/Platform/Core/Logger.h"
#include "tracy/Tracy.hpp"

#include "Engine/ECS/System/CCL_SystemRegistry.h"

void AnimLabScene::Initialize()
{
    BaseScene::Initialize();

    const std::string animLabLevel = "Assets/Scene/TestScene/AnimLab.level.json";

    ZoneScopedN("AnimLabScene: LoadLevel AnimLab");
    if (!LevelSerializer::LoadLevel(m_worldPtr, animLabLevel))
        CCL_LOG_ERROR(LogCategory::Core, "AnimLabScene: レベルの読み込みに失敗: %s", animLabLevel.c_str());
}



void AnimLabScene::Finalize()
{
    BaseScene::Finalize();
}



void AnimLabScene::RegisterSystems()
{
    auto &sysManager = *(m_ecsContext->GetSystemManager());
    CCL::ECS::SystemRegistry::Instance().RegisterAll(sysManager);
}



REGISTER_SCENE(AnimLabScene, "AnimLab")
