#include "BaseScene.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemManager.h"

#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Serialization/Factory/SceneInitializer.h"

#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Physics/JoltIntegration/JoltPhysicsManager.h"
#include "Engine/Physics/JoltIntegration/JoltPhysicsFacade.h"
#include "Engine/Audio/IAudioAPI.h"

#include "Engine/Platform/Input/Input.h"
#include "Engine/Platform/Input/InputFacade.h"

#include "Engine/Graphics/Systems/Core/EnvironmentSystem.h"
#include "Engine/Graphics/Renderer/RenderQueue.h"
#include "Engine/Graphics/Renderer/RenderDebugRegistry.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include "Engine/Core/Components/RenderInterpState.h"

#include "Engine/Core/App/Framework.h"
#include "Engine/Core/App/EngineServices.h"

#include "Engine/Transform/Systems/TransformUpdateSystem.h"
#include "Engine/Transform/Systems/HierarchyCleanupSystem.h"
#include "Engine/Physics/Systems/Jolt/JoltCleanupSystem.h"
#include "Engine/Audio/Systems/AudioCleanupSystem.h"

#include "Engine/UI/ECS/UIDrawList.h" // ECSネイティブUIの描画リスト
#include "Engine/UI/ECS/GameViewportInfo.h" // GameView表示矩形（UIクリック判定の座標写像用）
#include "Game/GameFlowState.h"        // ポーズ状態（ロジック停止の判定）

#include "tracy/Tracy.hpp"

#include "Engine/Platform/Core/Logger.h"

#include "Engine/Timer/TimeState.h"



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
    // フレーム並列用の共有ジョブプール（EngineServices所有）を注入する。
    // これで各Worldが自前のプールを持たず、アプリ全体で1プールを共有する。
    {
        ZoneScopedN("BaseScene: ECSContext create");
        _ecsContext = std::make_unique<CCL::ECS::ECSContext>(
            EngineServices::Instance().jobSystem.get());
    }
    _worldPtr   = _ecsContext->GetWorld(); // 互換性用

    // リソース登録
    _worldPtr->AddResource<Input *>(&Input::Instance());

    //  RenderInterpState は、RenderInterpSystem が参照するためにリソースとして登録する。
    _worldPtr->AddResource<RenderInterpState>();


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

    _worldPtr->AddResource<TimeContext>();

    // ゲーム画面(RT)の表示矩形。エディタ(GameViewWindow)が毎フレーム書き込み、UIInputSystem が
    // マウス座標をUI空間へ写像するのに使う。Shipping/全画面では valid=false のまま（恒等写像）。
    _worldPtr->AddResource<GameViewportInfo>();


    // システム登録 (派生クラスの処理が走る)。全システムのインスタンス生成・登録がここで走る。
    {
        ZoneScopedN("BaseScene: RegisterSystems");
        RegisterSystems();
    }

}

void BaseScene::Start() {}

void BaseScene::Finalize()
{
    // C++シーンそのものが消滅する前に、物理空間を安全に掃除する
    if (_worldPtr) {
        // 全エンティティに破壊予約をつける
        // (ここでは PersistentTag も関係なく全て破壊してよい。世界が終わるため)
        auto& chunks = _worldPtr->GetChunkManager().GetChunks();
        for (auto& chunkPtr : chunks) {
            if (!chunkPtr) continue;
            auto* chunk = chunkPtr.get();
            for (size_t i = 0; i < chunk->GetEntityCount(); ++i) {
                CCL::ECS::EntityID entity = chunk->GetEntityByIndex(i);
                if (entity != CCL::ECS::InvalidEntityID) {
                    TransformUpdateSystem::MarkForDestruction(*_worldPtr, entity, true);
                }
            }
        }

        // Jolt剛体の即時消去
        JoltCleanupSystem::ExecuteCleanup(*_worldPtr);
        // 破棄される AudioEmitter の再生中サウンドを止める（BGM等の鳴りっぱなし防止）
        AudioCleanupSystem::ExecuteCleanup(*_worldPtr);
        HierarchyCleanupSystem::ExecuteCleanup(*_worldPtr);

        // ゴミ箱を空にする
        _worldPtr->ScrutinyAndApply();
    }

    // この直後、SceneManager側のClear()によって _ecsContext が破棄され、
    // Worldもシステムも完全にメモリから解放されます。
}

void BaseScene::Update(float elapsedTime)
{
    ZoneScopedN("BaseScene::Update");

    RenderQueue::Instance().BeginFrame();
    RenderDebugRegistry::Instance().BeginFrame(); // デバッグテクスチャ一覧もフレーム先頭でクリア
    Graphics::Instance().GetParticleRenderer()->BeginFrame();

    // カメラ行列・CbScene の更新と CbLight のバインドを行う。
    // lightManager->Bind は Graphics::Update 内で実行される。
    Graphics::Instance().Update();


    if (_ecsContext) {
        // ポーズ中は UpdatePaused（構造変更の適用＋描画のみ）に切替える。
        // これによりポーズ中でもオーバーレイの生成/破棄が反映され、UIも描ける。
        const bool paused = _worldPtr && _worldPtr->HasResource<GameFlowState>()
                            && _worldPtr->GetResource<GameFlowState>().paused;
        if (paused)
            _ecsContext->UpdatePaused(elapsedTime);
        else
            _ecsContext->UpdateRender(elapsedTime);
    }

}

void BaseScene::FixedUpdate(float fixedTime)
{
    ZoneScopedN("BaseScene::FixedUpdate");

    // ポーズ中はゲームロジック・物理を止める（描画は Update 側で継続）。
    const bool paused = _worldPtr && _worldPtr->HasResource<GameFlowState>()
                        && _worldPtr->GetResource<GameFlowState>().paused;
    if (paused) return;

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

        // UI専用のα合成：色はストレートα合成のまま、RTのアルファは書き換えない。
        // これで不透明シーンのα=1が保たれ、テキスト透明部分にαの穴が開かず、
        // エディタ GameView(ImGui::Image) で「黒い枠」が出なくなる。
        dc->OMSetBlendState(
            gfx.GetRenderState()->GetBlendState(BlendState::UITransparency), nullptr, 0xffffffff);
        dc->OMSetDepthStencilState(
            gfx.GetRenderState()->GetDepthStencilState(DepthState::NoTestNoWrite), 0);

        // ECSネイティブUI: UILayoutSystem(UpdateRender)が構築した UIDrawList を描く。
        if (_worldPtr && _worldPtr->HasResource<UIDrawList>())
            _uiRenderer.Flush(dc, gfx.GetDevice(), _worldPtr->GetResource<UIDrawList>());

        OnRenderUI();

        // 画面遷移フェード（黒オーバーレイ）は SceneManager::Render が
        // 全シーン描画の後に一元的に被せる（ここでは描かない）。
    }

}

void BaseScene::DrawGUI()
{
    // ★拡張ポイント: ImGui描画
    OnDrawImGui();
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