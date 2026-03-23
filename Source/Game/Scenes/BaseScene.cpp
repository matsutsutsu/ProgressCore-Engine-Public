#include "BaseScene.h"
#include <imgui.h>


#include "Engine/Serialization/SceneSerializer.h"
#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Serialization/Factory/SceneInitializer.h"

#include "Engine/Graphics/Core/Graphics.h"
#include "Engine/Physics/JoltPhysicsManager.h"
#include "Game/Logics/Combat/GlobalDamageQueue.h"




#include "Engine/GamePlay/Graphics/Core/EnvironmentSystem.h"

void BaseScene::Initialize()
{
    auto &graphics = Graphics::Instance();

    ID3D11Device *device       = graphics.GetDevice();
    float         screenWidth  = graphics.GetScreenWidth();
    float         screenHeight = graphics.GetScreenHeight();


    // カメラ基本設定
    camera.SetPerspectiveFov(
        DirectX::XMConvertToRadians(45), screenWidth / screenHeight, 0.1f, 1000.0f);
    camera.SetLookAt({0, 10, -10}, {0, 0, 0}, {0, 1, 0});

    lightManager.Initialize(device);
    Prefab::Initialize(device);

    // ECSコンテキストの生成
    _ecsContext = std::make_unique<CCL::ECS::ECSContext>();
    _worldPtr   = _ecsContext->GetWorld(); // 互換性用

    // リソース登録
    _worldPtr->AddResource<GlobalDamageQueue>();

    // WorldのリソースとしてJoltを登録し、初期化する
    _worldPtr->AddResource<JoltPhysicsManager>();
    _worldPtr->GetResource<JoltPhysicsManager>().Initialize();

    // システムがコンストラクタ引数なしでもアクセスできるようにポインタを登録
    _worldPtr->AddResource<Camera *>(&camera);
    _worldPtr->AddResource<LightManager *>(&lightManager);
    _worldPtr->AddResource<PostProcessManager *>(&_postProcess);

    _worldPtr->AddResource<ModelRenderer *>(graphics.GetModelRenderer());
    _worldPtr->AddResource<ParticleRenderer *>(graphics.GetParticleRenderer());
    _worldPtr->AddResource<PrimitiveRenderer *>(graphics.GetPrimitiveRenderer());
    _worldPtr->AddResource<ShapeRenderer *>(graphics.GetShapeRenderer());

    // TriggerSystem 等がアクセスできるように、SystemManager のポインタをリソースとして登録
    _worldPtr->AddResource<CCL::ECS::SystemManager *>(_ecsContext->GetSystemManager());

    // システム登録 (派生クラスの処理が走る)
    RegisterSystems();

    _postProcess.Initialize(device, (int)screenWidth, (int)screenHeight);

    // GPUパーティクル事前準備　オブジェクトプール用に最初に確保しておく
    Prefab::PrewarmParticles(*_worldPtr);

    
   
}

void BaseScene::Start() {}

void BaseScene::Finalize()
{
 
}

void BaseScene::Update(float elapsedTime)
{
    // ロード予約チェック
    if (!_pendingLoadScenePath.empty()) {
        ExecuteLoadScene();
    }

    Graphics::Instance().GetModelRenderer()->BeginFrame();
    Graphics::Instance().GetParticleRenderer()->BeginFrame();

    // Graphics定数バッファ更新 (EditorSceneにあった処理を共通化)
    Graphics::Instance().Update(&camera);

    if (_ecsContext) {
       _ecsContext->UpdateRender(elapsedTime);
    }

}

void BaseScene::FixedUpdate(float fixedTime) 
{ 
    // ECSコンテキストに更新を委譲（これだけで全てが正しい順序で回る）
    if (_ecsContext) {
        _ecsContext->UpdateLogic(fixedTime);
    }
}

void BaseScene::Render()
{
    auto &graphics = Graphics::Instance();
    auto *dc       = graphics.GetDeviceContext();

    // 1. 描画準備 & ポストプロセス開始
    graphics.SetRenderTargets(false);
    graphics.Clear(0.65f, 0.65f, 1.0f, 1, false);

    // ブルームなどのポストプロセス開始
    float clearColor[] = {0.11, 0.16f, 0.25f, 1.0f};
    _postProcess.BeginScene(dc, clearColor, graphics.GetDepthStencilView());

    // 2. 3D描画コンテキスト設定
    RenderContext rc;
    rc.deviceContext = dc;
    rc.renderState   = graphics.GetRenderState();
    rc.camera        = &camera;
    rc.lightManager  = &lightManager;
    rc.shadowMap     = graphics.GetShadowMap();

    // 3. メイン描画フロー
    auto *modelRenderer = graphics.GetModelRenderer();
    modelRenderer->RenderShadowPass(rc);


    // =========================================================
    // 3. 舞台監督によるグローバルリソースのセット
    // =========================================================
    if (_ecsContext && _ecsContext->GetSystemManager()) {
        if (auto *envSystem = _ecsContext->GetSystemManager()->GetSystem<EnvironmentSystem>()) {
            // ここで CbScene の転送、フォグ、ノイズがセットされる
            envSystem->BindGlobalResources(dc);
        }
    }

    // =========================================================
    // 4. ライトとシャドウマップのバインド
    // =========================================================
    if (rc.lightManager) {
        rc.lightManager->Bind(dc, ShaderResources::CbLight);
    }
    if (rc.shadowMap) {
        rc.shadowMap->Bind(
            dc, ShaderResources::SrvShadow, ShaderResources::SmpShadow, ShaderResources::CbShadow);
    }

    // 1. 不透明なモデルを一気に描画（Zバッファを作る）
    modelRenderer->Render(rc);

    // 2. 透過物（パーティクル）を一斉描画
    // Zバッファを参照しながら、手前にあるものだけが正しく描画される
    auto *particleRenderer = graphics.GetParticleRenderer();
    if (particleRenderer) {
        particleRenderer->Render(rc.deviceContext);
    }

    // デバッグコリジョン描画
    // _world.DrawDebugCollision は消滅したので、SystemManagerから直接呼ぶ
    if (_ecsContext && _ecsContext->GetSystemManager()) {
        _ecsContext->GetSystemManager()->DrawDebugShape(graphics.GetShapeRenderer());
    }

    // =========================================================
    // ShapeRendererが画面に描画される【直前】に、
    // Joltの当たり判定をShapeRendererのキューに溜め込む
    // =========================================================
    //if (_worldPtr->HasResource<JoltPhysicsManager>()) {
    //    auto &joltManager = _worldPtr->GetResource<JoltPhysicsManager>();
    //    joltManager.DrawBodies(graphics.GetShapeRenderer());
    //}

    // DebugShape描画
    graphics.GetShapeRenderer()->Render(dc, camera.GetView(), camera.GetProjection());


    auto *primitiveRenderer = Graphics::Instance().GetPrimitiveRenderer();

    // グリッド描画
    primitiveRenderer->DrawGrid(20, 1);
    primitiveRenderer->Render(rc.deviceContext, camera.GetView(), camera.GetProjection());


    // 追加の3D描画 (Grid, Particleなど)
    OnRender3D(rc);

   
    // 4. ポストプロセス終了
    _postProcess.EndScene(dc, graphics.GetBackBufferRTV());

    dc->OMSetBlendState(rc.renderState->GetBlendState(BlendState::Transparency), nullptr, 0xffffffff);

    // =========================================================================
    // 深度テストを完全にオフにする（DepthState::None）
    // これにより、Depthが「No Resource」であってもGPUはエラーを出さず、
    // UIや文字のピクセルをすべて無条件で画面に焼き付けます。
    // =========================================================================
    dc->OMSetDepthStencilState(rc.renderState->GetDepthStencilState(DepthState::NoTestNoWrite), 0);

    // ゲーム内UI (Spriteなど)
    OnRenderUI();
}

void BaseScene::DrawGUI()
{
    // ★拡張ポイント: ImGui描画
    OnDrawImGui();
}

void BaseScene::ExecuteLoadScene()
{
    // ★拡張ポイント: ロード前処理 (パーティクルリセット等)
    OnPreLoadScene();


    // ファイルがある場合はロード、なければ何もしない(リセットのみ)
    if (!_pendingLoadScenePath.empty()) {
        // 拡張子判定などでロード方法を変えるならここで分岐
        SceneSerializer::Deserialize(_worldPtr, _pendingLoadScenePath);
    }


    _pendingLoadScenePath.clear();
}