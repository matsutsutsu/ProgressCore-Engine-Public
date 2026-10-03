#include "BaseScene.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemManager.h"

#include "Engine/Serialization/Factory/Prefab.h"

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

#include "Engine/UI/ECS/UIDrawList.h"       // ECSネイティブUIの描画リスト
#include "Engine/UI/ECS/GameViewportInfo.h" // GameView表示矩形（UIクリック判定の座標写像用）
#include "Engine/Core/App/GameFlowState.h"  // ポーズ状態（ロジック停止の判定）

#include "tracy/Tracy.hpp"

#include "Engine/Platform/Core/Logger.h"

#include "Engine/Time/TimeState.h"

void BaseScene::Initialize()
{
    ZoneScopedN("BaseScene::Initialize");

    auto         &gfx          = Graphics::Instance();
    ID3D11Device *device       = gfx.GetDevice();
    float         screenWidth  = gfx.GetScreenWidth();
    float         screenHeight = gfx.GetScreenHeight();

    // カメラの初期設定。
    //   カメラは Graphics が所有する。BaseScene はここで初期値を書くだけで、
    //   以降は CameraBrainSystem に任せる
    Camera *cam = gfx.GetMainCamera();
    cam->SetPerspectiveFov(
        DirectX::XMConvertToRadians(45.0f),
        gfx.GetScreenWidth() / gfx.GetScreenHeight(),
        0.1f, 1000.0f);
    cam->SetLookAt({0.0f, 10.0f, -10.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});

    // ECSContext を生成する。
    // フレーム並列用の共有ジョブプール（EngineServices 所有）を注入し、
    // 各 World が自前のプールを持たず、アプリ全体で1つのプールを共有する
    {
        ZoneScopedN("BaseScene: ECSContext create");
        m_ecsContext = std::make_unique<CCL::ECS::ECSContext>(
            EngineServices::Instance().jobSystem.get());
    }
    m_worldPtr = m_ecsContext->GetWorld();

    // 各 System が World から引けるよう、共通のサービスをリソースとして登録する
    m_worldPtr->AddResource<Input *>(&Input::Instance());

    // Framework が毎フレーム alpha を書き、TransformInterpolationSystem が読む
    m_worldPtr->AddResource<RenderInterpState>();

    auto &services = EngineServices::Instance();

    // 1. 入力と音はそのまま登録する
    m_worldPtr->AddResource<std::shared_ptr<IInputAPI>>(services.inputFacade);
    m_worldPtr->AddResource<std::shared_ptr<IAudioAPI>>(services.audioManager);

    // 2. 物理（Jolt）。JoltPhysicsManager 本体は EngineServices のものを共有する
    m_worldPtr->AddResource<JoltPhysicsManager *>(services.physicsManager.get());

    // ゲーム側は IPhysicsAPI として引くので、リソースの型は IPhysicsAPI の shared_ptr にする
    auto myFacade = std::make_shared<JoltPhysicsFacade>(m_worldPtr);
    m_worldPtr->AddResource<std::shared_ptr<IPhysicsAPI>>(myFacade);

    // Graphics が所有するサブシステムを ECS へ接続する。
    // ECS システムがライトを変更すれば Graphics::Update() の Bind に反映される。
    m_worldPtr->AddResource<Camera *>(cam);
    m_worldPtr->AddResource<LightManager *>(gfx.GetLightManager());
    m_worldPtr->AddResource<PostProcessManager *>(gfx.GetPostProcessManager());
    m_worldPtr->AddResource<ShadowMap *>(gfx.GetShadowMap());
    m_worldPtr->AddResource<ModelConstantBuffers *>(gfx.GetModelConstantBuffers());
    m_worldPtr->AddResource<ParticleRenderer *>(gfx.GetParticleRenderer());
    m_worldPtr->AddResource<PrimitiveRenderer *>(gfx.GetPrimitiveRenderer());
    m_worldPtr->AddResource<ShapeRenderer *>(gfx.GetShapeRenderer());

    m_worldPtr->AddResource<TimeContext>();

    // ゲーム画面（RT）の表示矩形。エディタ（GameViewWindow）が毎フレーム書き込み、UIInputSystem が
    // マウス座標を UI 空間へ写像するのに使う。Shipping/全画面では valid=false のまま（変換なし）
    m_worldPtr->AddResource<GameViewportInfo>();

    // 派生クラスの System 登録。リソースを全て登録した後に呼ぶ（System の Initialize がリソースを読むため）
    {
        ZoneScopedN("BaseScene: RegisterSystems");
        RegisterSystems();
    }
}

void BaseScene::Start() {}

void BaseScene::Finalize()
{
    // シーンが消える前に、共有の物理空間・オーディオを掃除する
    if (m_worldPtr)
    {
        // 全 Entity に破棄の印を付ける
        // （World ごと消えるので、ここでは PersistentTag も関係なく全て破棄してよい）
        auto &chunks = m_worldPtr->GetChunkManager().GetChunks();
        for (auto &chunkPtr : chunks)
        {
            if (!chunkPtr) continue;
            auto *chunk = chunkPtr.get();
            for (size_t i = 0; i < chunk->GetEntityCount(); ++i)
            {
                CCL::ECS::EntityID entity = chunk->GetEntityByIndex(i);
                if (entity != CCL::ECS::InvalidEntityID)
                {
                    TransformUpdateSystem::MarkForDestruction(*m_worldPtr, entity, true);
                }
            }
        }

        // DestroyTag を「今すぐ」適用する。AddComponent<DestroyTag> は遅延(pendingOps)なので、
        //   これを挟まないと下の JoltCleanup が「DestroyTag付きチャンク」を1つも見つけられず、
        //   共有の Jolt 空間に前シーンの剛体が残る（再戦時に見えないコライダーで引っかかる）。
        m_worldPtr->ScrutinyAndApply();

        JoltCleanupSystem::ExecuteCleanup(*m_worldPtr);

        // 音の後始末は「共有オーディオエンジンを丸ごと黙らせる」StopAll に一本化する。
        //   このシーンの World はこの直後に完全破棄されるので、per-emitter の StopEvent
        //   （DestroyTag が正しく付き playingId が正確、という前提に依存＝取りこぼしやすい）
        //   ではなく、前提の要らない StopAll で確実に止める（再戦時に BGM が二重に鳴るのを防ぐ）。
        if (m_worldPtr->HasResource<std::shared_ptr<IAudioAPI>>())
        {
            if (auto &audio = m_worldPtr->GetResource<std::shared_ptr<IAudioAPI>>())
                audio->StopAll();
        }

        HierarchyCleanupSystem::ExecuteCleanup(*m_worldPtr);

        // 積んだ破棄予約を確定させる
        m_worldPtr->ScrutinyAndApply();
    }

    // この直後、SceneManager::Clear() がシーンを delete し、m_ecsContext ごと
    // World も System も解放される
}

void BaseScene::Update(float elapsedTime)
{
    ZoneScopedN("BaseScene::Update");

    {
        ZoneScopedN("BaseScene: RenderQueue/DebugRegistry BeginFrame");
        RenderQueue::Instance().BeginFrame();
        RenderDebugRegistry::Instance().BeginFrame(); // デバッグ用テクスチャの一覧もフレーム先頭でクリアする
        Graphics::Instance().GetParticleRenderer()->BeginFrame();
    }

    {
        ZoneScopedN("BaseScene: Graphics::Update");
        // カメラ行列・CbScene の更新と CbLight のバインドを行う
        Graphics::Instance().Update();
    }

    if (m_ecsContext)
    {
        // ポーズ中は UpdatePaused（構造変更の適用 + 描画のみ）に切り替える。
        // ポーズ中でもオーバーレイの生成/破棄が反映され、UI も描ける
        const bool paused = m_worldPtr && m_worldPtr->HasResource<GameFlowState>() && m_worldPtr->GetResource<GameFlowState>().paused;
        if (paused)
            m_ecsContext->UpdatePaused(elapsedTime);
        else
            m_ecsContext->UpdateRender(elapsedTime);
    }
}

void BaseScene::FixedUpdate(float fixedTime)
{
    ZoneScopedN("BaseScene::FixedUpdate");

    // ポーズ中はゲームロジック・物理を止める（描画は Update 側で継続）。
    const bool paused = m_worldPtr && m_worldPtr->HasResource<GameFlowState>() && m_worldPtr->GetResource<GameFlowState>().paused;
    if (paused) return;

    // System の実行順は ECSContext / SystemManager が決める
    if (m_ecsContext)
    {
        m_ecsContext->UpdateLogic(fixedTime);
    }
}

void BaseScene::Render()
{
    ZoneScopedN("BaseScene::Render Pipeline");

    auto &gfx = Graphics::Instance();
    auto *dc  = gfx.GetDeviceContext();

    // EnvironmentSystem がフォグ / IBL / ノイズと CbScene をバインドする。
    // CbScene の中身は触らない（Graphics::Update() が書き込み済み）
    if (m_ecsContext && m_ecsContext->GetSystemManager())
    {
        if (auto *env = m_ecsContext->GetSystemManager()->GetSystem<EnvironmentSystem>())
        {
            const RenderServices &svc = gfx.GetRenderServices();
            env->BindGlobalResources(dc, svc);
        }
    }

    // 描画先を決めてフレームコンテキストを作り、描画パイプラインを一括実行する
#ifdef ENABLE_EDITOR
    auto ctx = gfx.MakeFrameContext(gfx.GetGameRTV());
#else
    auto ctx = gfx.MakeFrameContext(gfx.GetBackBufferRTV());
#endif

    gfx.Render(ctx);

    // 2D UI
    {
        ZoneScopedN("UI Render");

        // UI 専用のα合成: 色はストレートα合成のまま、RT のアルファは書き換えない。
        // 不透明シーンの α=1 が保たれ、文字の透明部分に α の穴が開かないので、
        // エディタの GameView（ImGui::Image）で「黒い枠」が出ない
        dc->OMSetBlendState(
            gfx.GetRenderState()->GetBlendState(BlendState::UITransparency), nullptr, 0xffffffff);
        dc->OMSetDepthStencilState(
            gfx.GetRenderState()->GetDepthStencilState(DepthState::NoTestNoWrite), 0);

        // ECS の UI: 各 UI 収集 System（R10b）が積んだ UIDrawList を描く
        if (m_worldPtr && m_worldPtr->HasResource<UIDrawList>())
            m_uiRenderer.Flush(dc, gfx.GetDevice(), m_worldPtr->GetResource<UIDrawList>(),
                               gfx.GetRenderState());

        OnRenderUI();

        // 画面遷移フェード（黒オーバーレイ）は SceneManager::Render が
        // 全シーン描画の後に一元的に被せる（ここでは描かない）。
    }
}

void BaseScene::DrawGUI()
{
    OnDrawImGui();
}

void BaseScene::OnDeviceRecreated()
{
    // Graphics::RecoverFromDeviceLost() が内部で Shutdown → Initialize を行い、
    // D3D パイプライン・シェーダー・PostProcessManager は全て作り直された状態でここに来る。
    // World を作り直して Initialize → Start を呼び直すことで、ResourceManager に
    // ロードし直された新しいリソースを使うようにする（ゲームの進行状態はリセットされる）

    CCL_LOG_INFO(LogCategory::Core, "[BaseScene] OnDeviceRecreated: Device lost recovered. Reinitializing scene...\n");

    // 古い World を破棄する。全 Entity・コンポーネント・System と、旧リソースへのハンドルが解放される
    m_ecsContext.reset();
    m_worldPtr = nullptr;

    // シーンを最初から初期化し直す（シーンファイルのロードは派生クラスの Initialize / Start が行う）
    Initialize();
    Start();

    CCL_LOG_INFO(LogCategory::Core, "[BaseScene] OnDeviceRecreated: Complete.");
}
