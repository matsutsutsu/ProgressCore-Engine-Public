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

 
#include "tracy/Tracy.hpp"

#include "Engine/Platform/Core/Logger.h"

#include "Game/Logics/Wand/Bullet/MagicBulletPool.h"
#include "Game/Logics/Wand/Compiler/WandProgramRegistry.h"
#include "Game/Logics/Wand/Collision/BulletSpatialHash.h"



void BaseScene::Initialize()
{
    ZoneScopedN("BaseScene::Initialize");


    auto& gfx = Graphics::Instance();
    ID3D11Device* device = gfx.GetDevice();
    float screenWidth = gfx.GetScreenWidth();
    float screenHeight = gfx.GetScreenHeight();


    // =========================================================
    // カメラ初期設定
    //   Graphics が _mainCamera を所有。BaseScene はここで初期値を書き込み、
    //   以降は CameraBrainSystem に全て任せる。
    // =========================================================
    Camera* cam = gfx.GetMainCamera();
    cam->SetPerspectiveFov(
        DirectX::XMConvertToRadians(45.0f),
        gfx.GetScreenWidth() / gfx.GetScreenHeight(),
        0.1f, 1000.0f);
    cam->SetLookAt({ 0.0f, 10.0f, -10.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });

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
    _worldPtr->AddResource<Camera*>(cam);
    _worldPtr->AddResource<LightManager*>(gfx.GetLightManager());
    _worldPtr->AddResource<PostProcessManager*>(gfx.GetPostProcessManager());
    _worldPtr->AddResource<ShadowMap*>(gfx.GetShadowMap());
    _worldPtr->AddResource<ModelRenderer*>(gfx.GetModelRenderer());
    _worldPtr->AddResource<ParticleRenderer*>(gfx.GetParticleRenderer());
    _worldPtr->AddResource<PrimitiveRenderer*>(gfx.GetPrimitiveRenderer());
    _worldPtr->AddResource<ShapeRenderer*>(gfx.GetShapeRenderer());

    // SystemManager のポインタをリソースとして登録
    //_worldPtr->AddResource<CCL::ECS::SystemManager *>(_ecsContext->GetSystemManager());


    // ゲームシステム用のリソース登録
    _worldPtr->AddResource<WandProgramRegistry>();
    _worldPtr->AddResource<MagicBulletPool>(); // 既定 capacity=8192
    _worldPtr->AddResource<BulletSpatialHash>();

    // システム登録 (派生クラスの処理が走る)
    RegisterSystems();

    // GPUパーティクル事前準備　オブジェクトプール用に最初に確保しておく
    //Prefab::PrewarmParticles(*_worldPtr);

}

void BaseScene::Start() {}

void BaseScene::Finalize() {}

void BaseScene::Update(float elapsedTime)
{
    ZoneScopedN("BaseScene::Update");


    // ECSシステムが走り出す「前」の、誰もメモリを触っていない安全な瞬間に世界を切り替える！
    ExecutePendingSceneOperations();

    RenderQueue::Instance().BeginFrame();
    Graphics::Instance().GetParticleRenderer()->BeginFrame();

    // カメラ行列・CbScene の更新と CbLight のバインドを行う。
    // lightManager->Bind は Graphics::Update 内で実行される。
    Graphics::Instance().Update();


    if (_ecsContext) {
       _ecsContext->UpdateRender(elapsedTime);
    }

}

void BaseScene::FixedUpdate(float fixedTime) 
{  
    ZoneScopedN("BaseScene::FixedUpdate");


    // ECSコンテキストに更新を委譲（これだけで全てが正しい順序で回る）
    if (_ecsContext) {
        _ecsContext->UpdateLogic(fixedTime);
    }
}

void BaseScene::Render()
{
    ZoneScopedN("BaseScene::Render Pipeline"); // シーン描画全体



    auto& gfx = Graphics::Instance();
    auto* dc = gfx.GetDeviceContext();

    // EnvironmentSystem: フォグ / IBL / ノイズのバインド + CbScene のバインド
    // ★ CbScene の中身は触らない（Graphics::Update() が書き込み済み）
    if (_ecsContext && _ecsContext->GetSystemManager()) {
        if (auto* env = _ecsContext->GetSystemManager()->GetSystem<EnvironmentSystem>()) {
            const RenderServices& svc = gfx.GetRenderServices();
            env->BindGlobalResources(dc, svc);
        }
    }

    // MakeFrameContext でフレームコンテキストを構築し、パイプラインを一括実行する。
#ifdef ENABLE_EDITOR
    auto ctx = gfx.MakeFrameContext(gfx.GetGameRTV());
#else
    auto ctx = gfx.MakeFrameContext(gfx.GetBackBufferRTV());
#endif

    gfx.Render(ctx); // ← Graphics が直接パイプラインを回す
 

   
    // 2D UI
    {
        ZoneScopedN("UI Render");

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

// ============================================================
//  OnDeviceRecreated（デバイスロスト復帰後）
// ============================================================
/**
 * @brief GPU デバイス復帰後に Framework から呼ばれるデフォルト実装。
 *
 * @details
 *  Graphics::RecoverFromDeviceLost() が内部で Shutdown → Initialize を行い、
 *  D3D パイプライン・シェーダー・PostProcessManager はすべて再構築済みの状態でここに来る。
 *
 *  この実装では ECS ワールドを作り直して Initialize → Start を再呼び出しする。
 *  これにより ResourceManager にロードし直された新しいリソースで正常動作する。
 *
 *  ⚠️ ゲームの進行状態（エンティティ位置・HP など）はリセットされる。
 *  状態を保持したい場合は派生クラスでオーバーライドし、
 *  セーブデータから復元するなどの処理を追加してください。
 */
void BaseScene::OnDeviceRecreated()
{
	CCL_LOG_INFO(LogCategory::Core,"[BaseScene] OnDeviceRecreated: Device lost recovered. Reinitializing scene...\n");
    
    // --- 旧 ECS ワールドを安全に破棄 ---
    // _ecsContext がリセットされると、内部のすべてのエンティティ・コンポーネント・
    // システムが解放される。この時点で旧リソースのハンドルも無効になる。
    _ecsContext.reset();
    _worldPtr = nullptr;

    // --- シーンを完全に再初期化 ---
    // Initialize() でECSインフラを再構築 → RegisterSystems() で全システムを再登録
    // シーンファイルのロードが必要な場合は派生クラスの Initialize() または Start() で行う
    Initialize();
    Start();

    CCL_LOG_INFO(LogCategory::Core, "[BaseScene] OnDeviceRecreated: Complete.");
}