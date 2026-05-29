#include "BaseScene.h"


#include "Engine/Serialization/Serializer/SceneSerializer.h"
#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Serialization/Factory/SceneInitializer.h"

#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Physics/JoltIntegration/JoltPhysicsManager.h"
#include "Engine/Physics/JoltIntegration/JoltPhysicsFacade.h"
#include "Engine/Audio/FmodIntegration/FmodAudioManager.h"

#include "Engine/Platform/Input/Input.h"
#include "Engine/Platform/Input/InputFacade.h"

#include "Engine/Graphics/Systems/Core/EnvironmentSystem.h"
#include "Engine/Graphics/Renderer/RenderQueue.h"
#include "Engine/Graphics/Shader/ShaderResources.h"

#include "Engine/Core/App/Framework.h"
#include "Engine/Core/App/EngineServices.h"

#if TRACY_ENABLE
#include "tracy/Tracy.hpp"
#endif

#include "Engine/Platform/Core/Logger.h"


void BaseScene::Initialize()
{
#if TRACY_ENABLE
    ZoneScopedN("BaseScene::Initialize");
#endif

    auto& gfx = Graphics::Instance();
    ID3D11Device* device = gfx.GetDevice();
    float screenWidth = gfx.GetScreenWidth();
    float screenHeight = gfx.GetScreenHeight();


    // カメラ基本設定
    camera.SetPerspectiveFov(
        DirectX::XMConvertToRadians(45), screenWidth / screenHeight, 0.1f, 1000.0f);
    camera.SetLookAt({0, 10, -10}, {0, 0, 0}, {0, 1, 0});

    Prefab::Initialize(device);

    // ECSコンテキストの生成
    _ecsContext = std::make_unique<CCL::ECS::ECSContext>();
    _worldPtr   = _ecsContext->GetWorld(); // 互換性用

    // リソース登録
    _worldPtr->AddResource<Input *>(&Input::Instance());


    auto& services = EngineServices::Instance();

    // 1. InputとAudioは、そのままResourceに登録
    _worldPtr->AddResource<std::shared_ptr<IInputAPI>>(services.inputFacade);
    _worldPtr->AddResource<std::shared_ptr<IAudioAPI>>(services.audioManager);

    // 2. Physics (Jolt)
    // JoltPhysicsManager 本体はEngineServicesのものを使う
    _worldPtr->AddResource<JoltPhysicsManager*>(services.physicsManager.get());

    // 箱の型として「IPhysicsAPIのshared_ptr」を明示し、中身として「Joltのshared_ptr」を渡す
    auto myFacade = std::make_shared<JoltPhysicsFacade>(_worldPtr);
    _worldPtr->AddResource<std::shared_ptr<IPhysicsAPI>>(myFacade);



    // Graphics が所有するサブシステムを ECS へ接続する。
    // ECS システムがライトを変更すれば Graphics::Update() の Bind に反映される。
    _worldPtr->AddResource<LightManager*>(gfx.GetLightManager());
    _worldPtr->AddResource<PostProcessManager*>(gfx.GetPostProcessManager());
    _worldPtr->AddResource<ShadowMap*>(gfx.GetShadowMap());
    _worldPtr->AddResource<ModelRenderer*>(gfx.GetModelRenderer());
    _worldPtr->AddResource<ParticleRenderer*>(gfx.GetParticleRenderer());
    _worldPtr->AddResource<PrimitiveRenderer*>(gfx.GetPrimitiveRenderer());
    _worldPtr->AddResource<ShapeRenderer*>(gfx.GetShapeRenderer());

    // TriggerSystem 等がアクセスできるように、SystemManager のポインタをリソースとして登録
    _worldPtr->AddResource<CCL::ECS::SystemManager *>(_ecsContext->GetSystemManager());

    // システム登録 (派生クラスの処理が走る)
    RegisterSystems();

    // GPUパーティクル事前準備　オブジェクトプール用に最初に確保しておく
    //Prefab::PrewarmParticles(*_worldPtr);

}

void BaseScene::Start() {}

void BaseScene::Finalize() {}

void BaseScene::Update(float elapsedTime)
{
#if TRACY_ENABLE
    ZoneScopedN("BaseScene::Update");
#endif

    // ECSシステムが走り出す「前」の、誰もメモリを触っていない安全な瞬間に世界を切り替える！
    ExecutePendingSceneOperations();

    RenderQueue::Instance().BeginFrame();
    Graphics::Instance().GetParticleRenderer()->BeginFrame();

    // カメラ行列・CbScene の更新と CbLight のバインドを行う。
    // lightManager->Bind は Graphics::Update 内で実行される。
    Graphics::Instance().Update(&camera);


    if (_ecsContext) {
       _ecsContext->UpdateRender(elapsedTime);
    }

}

void BaseScene::FixedUpdate(float fixedTime) 
{ 
#if TRACY_ENABLE
    ZoneScopedN("BaseScene::FixedUpdate");
#endif

    // ECSコンテキストに更新を委譲（これだけで全てが正しい順序で回る）
    if (_ecsContext) {
        _ecsContext->UpdateLogic(fixedTime);
    }
}

void BaseScene::Render()
{
#if TRACY_ENABLE
    ZoneScopedN("BaseScene::Render Pipeline"); // シーン描画全体
#endif


    auto& gfx = Graphics::Instance();
    auto* dc = gfx.GetDeviceContext();

    // EnvironmentSystem: CbScene, フォグ, IBL, ノイズをバインドする。
        // BindGlobalResources に RenderServices を渡すことで Graphics::Instance() を除去した。
    if (_ecsContext && _ecsContext->GetSystemManager()) {
        if (auto* env = _ecsContext->GetSystemManager()->GetSystem<EnvironmentSystem>()) {
            const RenderServices& svc = gfx.GetModelRenderer()->GetServices();
            env->BindGlobalResources(dc, svc);
        }
    }

    // MakeFrameContext でフレームコンテキストを構築し、パイプラインを一括実行する。
    // 旧 RenderContext 手動構築（50+ 行）が以下の数行に集約される。
#ifdef ENABLE_EDITOR
    auto ctx = gfx.MakeFrameContext(&camera, gfx.GetGameRTV());
#else
    auto ctx = gfx.MakeFrameContext(&camera, gfx.GetBackBufferRTV());
#endif

    gfx.GetModelRenderer()->Render(ctx);
 

   
    // 2D UI
    {
#if TRACY_ENABLE
        ZoneScopedN("UI Render");
#endif
        dc->OMSetBlendState(
            gfx.GetRenderState()->GetBlendState(BlendState::Transparency), nullptr, 0xffffffff);
        dc->OMSetDepthStencilState(
            gfx.GetRenderState()->GetDepthStencilState(DepthState::NoTestNoWrite), 0);

        OnRenderUI();
    }

}

void BaseScene::DrawGUI()
{
    // ★拡張ポイント: ImGui描画
    OnDrawImGui();
}



// ---------------------------------------------------------
// 予約されたセーブ/ロード処理の消化 (フレームの先頭で呼ばれる)
// ---------------------------------------------------------
void BaseScene::ExecutePendingSceneOperations()
{
    // ロード予約がある場合
    if (!_pendingLoadScenePath.empty()) {
        // ★必要であればここでパーティクル等の事前リセット処理 (OnPreLoadScene) を呼ぶ

        SceneManager::Instance().LoadScene(_worldPtr, _pendingLoadScenePath);
        _pendingLoadScenePath.clear();
    }

    // セーブ予約がある場合
    if (!_pendingSaveScenePath.empty()) {
        SceneManager::Instance().SaveScene(_worldPtr, _pendingSaveScenePath);
        _pendingSaveScenePath.clear();
    }
}