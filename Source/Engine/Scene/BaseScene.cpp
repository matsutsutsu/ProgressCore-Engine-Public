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

#include "Engine/UI/ECS/UIDrawList.h" // ECSネイティブUIの描画リスト
#include "Engine/UI/ECS/GameViewportInfo.h" // GameView表示矩形（UIクリック判定の座標写像用）
#include "Engine/Core/App/GameFlowState.h"        // ポーズ状態（ロジック停止の判定）

#include "tracy/Tracy.hpp"

#include "Engine/Platform/Core/Logger.h"

#include "Engine/Time/TimeState.h"



void BaseScene::Initialize()
{
    ZoneScopedN("BaseScene::Initialize");


    auto& gfx = Graphics::Instance();
    ID3D11Device* device = gfx.GetDevice();
    float screenWidth = gfx.GetScreenWidth();
    float screenHeight = gfx.GetScreenHeight();


    // =========================================================
    // カメラ初期設定
    //   Graphics が m_mainCamera を所有。BaseScene はここで初期値を書き込み、
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
        m_ecsContext = std::make_unique<CCL::ECS::ECSContext>(
            EngineServices::Instance().jobSystem.get());
    }
    m_worldPtr   = m_ecsContext->GetWorld(); // 互換性用

    // リソース登録
    m_worldPtr->AddResource<Input *>(&Input::Instance());

    //  RenderInterpState は、RenderInterpSystem が参照するためにリソースとして登録する。
    m_worldPtr->AddResource<RenderInterpState>();


    auto& services = EngineServices::Instance();

    // 1. InputとAudioは、そのままResourceに登録
    m_worldPtr->AddResource<std::shared_ptr<IInputAPI>>(services.inputFacade);
    m_worldPtr->AddResource<std::shared_ptr<IAudioAPI>>(services.audioManager);

    // 2. Physics (Jolt)
    // JoltPhysicsManager 本体はEngineServicesのものを使う
    m_worldPtr->AddResource<JoltPhysicsManager*>(services.physicsManager.get());

    // 箱の型として「IPhysicsAPIのshared_ptr」を明示し、中身として「Joltのshared_ptr」を渡す
    auto myFacade = std::make_shared<JoltPhysicsFacade>(m_worldPtr);
    m_worldPtr->AddResource<std::shared_ptr<IPhysicsAPI>>(myFacade);

    // Graphics が所有するサブシステムを ECS へ接続する。
    // ECS システムがライトを変更すれば Graphics::Update() の Bind に反映される。
    m_worldPtr->AddResource<Camera*>(cam);
    m_worldPtr->AddResource<LightManager*>(gfx.GetLightManager());
    m_worldPtr->AddResource<PostProcessManager*>(gfx.GetPostProcessManager());
    m_worldPtr->AddResource<ShadowMap*>(gfx.GetShadowMap());
    m_worldPtr->AddResource<ModelRenderer*>(gfx.GetModelRenderer());
    m_worldPtr->AddResource<ParticleRenderer*>(gfx.GetParticleRenderer());
    m_worldPtr->AddResource<PrimitiveRenderer*>(gfx.GetPrimitiveRenderer());
    m_worldPtr->AddResource<ShapeRenderer*>(gfx.GetShapeRenderer());

    // SystemManager のポインタをリソースとして登録
    //m_worldPtr->AddResource<CCL::ECS::SystemManager *>(m_ecsContext->GetSystemManager());

    m_worldPtr->AddResource<TimeContext>();

    // ゲーム画面(RT)の表示矩形。エディタ(GameViewWindow)が毎フレーム書き込み、UIInputSystem が
    // マウス座標をUI空間へ写像するのに使う。Shipping/全画面では valid=false のまま（恒等写像）。
    m_worldPtr->AddResource<GameViewportInfo>();


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
    if (m_worldPtr) {
        // 全エンティティに破壊予約をつける
        // (ここでは PersistentTag も関係なく全て破壊してよい。世界が終わるため)
        auto& chunks = m_worldPtr->GetChunkManager().GetChunks();
        for (auto& chunkPtr : chunks) {
            if (!chunkPtr) continue;
            auto* chunk = chunkPtr.get();
            for (size_t i = 0; i < chunk->GetEntityCount(); ++i) {
                CCL::ECS::EntityID entity = chunk->GetEntityByIndex(i);
                if (entity != CCL::ECS::InvalidEntityID) {
                    TransformUpdateSystem::MarkForDestruction(*m_worldPtr, entity, true);
                }
            }
        }

        // ★ DestroyTag を「今すぐ」適用する。AddComponent<DestroyTag> は遅延(pendingOps)なので、
        //   これを挟まないと下の JoltCleanup が「DestroyTag付きチャンク」を1つも見つけられず、
        //   共有 Jolt 空間に前シーンの剛体が残る（再戦時に見えないコライダーで引っかかる原因）。
        m_worldPtr->ScrutinyAndApply();

        // Jolt剛体の即時消去
        JoltCleanupSystem::ExecuteCleanup(*m_worldPtr);

        // ★ 音の後始末は「共有オーディオエンジンを丸ごと黙らせる」StopAll に一本化する。
        //   このシーンの World はこの直後に完全破棄されるので、per-emitter の StopEvent
        //   （DestroyTagが正しく付き playingId が正確、という前提に依存＝取りこぼしやすい）
        //   ではなく、前提ゼロの StopAll で確実に止める。再戦時にBGMが二重に鳴る不具合の根治。
        if (m_worldPtr->HasResource<std::shared_ptr<IAudioAPI>>()) {
            if (auto& audio = m_worldPtr->GetResource<std::shared_ptr<IAudioAPI>>())
                audio->StopAll();
        }

        HierarchyCleanupSystem::ExecuteCleanup(*m_worldPtr);

        // ゴミ箱を空にする
        m_worldPtr->ScrutinyAndApply();
    }

    // この直後、SceneManager側のClear()によって m_ecsContext が破棄され、
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


    if (m_ecsContext) {
        // ポーズ中は UpdatePaused（構造変更の適用＋描画のみ）に切替える。
        // これによりポーズ中でもオーバーレイの生成/破棄が反映され、UIも描ける。
        const bool paused = m_worldPtr && m_worldPtr->HasResource<GameFlowState>()
                            && m_worldPtr->GetResource<GameFlowState>().paused;
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
    const bool paused = m_worldPtr && m_worldPtr->HasResource<GameFlowState>()
                        && m_worldPtr->GetResource<GameFlowState>().paused;
    if (paused) return;

    // ECSコンテキストに更新を委譲（これだけで全てが正しい順序で回る）
    if (m_ecsContext) {
        m_ecsContext->UpdateLogic(fixedTime);
    }
}

void BaseScene::Render()
{
    ZoneScopedN("BaseScene::Render Pipeline"); // シーン描画全体



    auto& gfx = Graphics::Instance();
    auto* dc = gfx.GetDeviceContext();

    // EnvironmentSystem: フォグ / IBL / ノイズのバインド + CbScene のバインド
    // CbScene の中身は触らない（Graphics::Update() が書き込み済み）
    if (m_ecsContext && m_ecsContext->GetSystemManager()) {
        if (auto* env = m_ecsContext->GetSystemManager()->GetSystem<EnvironmentSystem>()) {
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
    // 拡張ポイント: ImGui描画
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
    // m_ecsContext がリセットされると、内部のすべてのエンティティ・コンポーネント・
    // システムが解放される。この時点で旧リソースのハンドルも無効になる。
    m_ecsContext.reset();
    m_worldPtr = nullptr;

    // --- シーンを完全に再初期化 ---
    // Initialize() でECSインフラを再構築 → RegisterSystems() で全システムを再登録
    // シーンファイルのロードが必要な場合は派生クラスの Initialize() または Start() で行う
    Initialize();
    Start();

    CCL_LOG_INFO(LogCategory::Core, "[BaseScene] OnDeviceRecreated: Complete.");
}
