#include "ShadowMapRenderSystem.h"

// 実装詳細で必要なインクルード
#include "ECS/Core/CCL_World.h"
#include "Engine/GamePlay/Graphics/Lighting/ShadowMapConfigComponent.h"

// 前方宣言されていたクラスのヘッダー実体
// (パスはプロジェクト構成に合わせて調整してください)
#include "Engine/Graphics/Core/Camera.h"
#include "Engine/Graphics/Renderer/ModelRenderer.h"
#include "Engine/Graphics/Resource/Model.h"

#include "Engine/Graphics/Renderer/RenderQueue.h"

#include <DirectXMath.h>
#include <cmath>

// システムの実行順序の定義ヘッダー
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

using namespace CCL::ECS;
using namespace DirectX;



ShadowMapRenderSystem::ShadowMapRenderSystem()
    : IfSystem("ShadowMapRenderSystem")
{
}

void ShadowMapRenderSystem::Update(float dt)
{
    // シャドウマップへの描画
    RenderShadowMap();
}

void ShadowMapRenderSystem::RenderShadowMap()
{
    // Worldからリソースとして取得する
    if (!_world->HasResource<Camera *>() || !_world->HasResource<LightManager *>() ||
        !_world->HasResource<ModelRenderer *>())  return;

    auto *renderer     = _world->GetResource<ModelRenderer *>();
    auto *camera       = _world->GetResource<Camera *>();
    auto *lightManager = _world->GetResource<LightManager *>();

    if (!renderer || !camera || !lightManager) return;

    // コンポーネントから設定値を取得
    // シーン内に ShadowMapConfigComponent を持つエンティティが1つある前提で探す
    // (無ければデフォルト値を使う)
    float    orthoSize   = 35.0f;
    float    shadowBias  = 0.0015f;
    XMFLOAT4 shadowColor = {0.0f, 0.0f, 0.0f, 0.5f};

    // 検索（ECSのView機能を使用）
    if (_world) {
        auto view = _world->View<ShadowMapConfigComponent>();
        for (auto entity : view) {
            auto *config = _world->GetComponent<ShadowMapConfigComponent>(entity);
            if (config) {
                orthoSize   = config->orthoSize;
                shadowBias  = config->shadowBias;
                shadowColor = config->shadowColor;
                break; // 1つ見つけたら終了
            }
        }
    }

    // --- 1. ライトビュー・プロジェクション行列の計算 ---

    // ターゲットを「メインカメラの注視点」にする
    // これにより、プレイヤーがどこへ行っても影がついてきます
    XMFLOAT3 cameraPos = camera->GetEye();
    XMFLOAT3 cameraDir = camera->GetFront();

    // カメラの少し先(例:20m先)を中心にする
    XMVECTOR vPos = XMLoadFloat3(&cameraPos);
    XMVECTOR vDir = XMLoadFloat3(&cameraDir);

    // カメラの20m先をターゲットにする (vPos + vDir * 20.0f)
    XMVECTOR targetV = XMVectorAdd(vPos, XMVectorScale(vDir, 20.0f));
    XMFLOAT3 lightTarget;
    XMStoreFloat3(&lightTarget, targetV);

    // ライトの向き
    const auto &dirLight = lightManager->GetDirectionalLight();
    XMVECTOR    L        = XMLoadFloat3(&dirLight.direction);
    L                    = XMVector3Normalize(L);

    // ライトの位置（ターゲットから逆方向に離す）
    float    distanceToLight = 100.0f; // 十分遠くから
    XMVECTOR lightPosV       = targetV - L * distanceToLight;
    XMFLOAT3 lightPos;
    XMStoreFloat3(&lightPos, lightPosV);

    // Upベクトルの計算を堅牢にする
    // ライトの向き(L)とUp(0,1,0)が平行に近いとLookAt計算が壊れる(NaNになる)ため対策する
    XMVECTOR Up  = XMVectorSet(0, 1, 0, 0);
    float    dot = XMVectorGetX(XMVector3Dot(L, Up)); // LとUpの内積

    // 内積が1に近い(=平行)なら、UpベクトルをZ軸に変える
    if (std::abs(dot) > 0.99f) {
        Up = XMVectorSet(0, 0, 1, 0);
    }

    // 仮想カメラ（ライト視点）の設定
    // Cameraクラスのインスタンスを一時的に作るか、ShadowMap専用のCameraメンバを持つのが良いです
    static Camera shadowCamera;
    shadowCamera.SetLookAt(lightPos, lightTarget, {0, 1, 0});
    shadowCamera.SetOrthographic(orthoSize, orthoSize, 1.0f, 200.0f);

    XMFLOAT4X4 lightVP = shadowCamera.GetViewProjectionMatrix();

    // 行列自体がNaNになっていないか念のためチェック
    XMMATRIX mLightVP = XMLoadFloat4x4(&lightVP);
    if (XMMatrixIsNaN(mLightVP)) {
        // 万が一壊れていたら単位行列などでリセットしてクラッシュ回避
        mLightVP = XMMatrixIdentity();
        XMStoreFloat4x4(&lightVP, mLightVP);
    }

    // 計算した行列をレンダラーに渡す
    renderer->SetShadowLightViewProjection(lightVP);

    renderer->SetShadowParams(shadowBias, shadowColor);

    // レンダラーから最新の視錐台を取得
    BoundingFrustum frustum = renderer->GetFrustum();

  
    auto& renderQueue = RenderQueue::Instance();

    ForEach([&](const TransformComponent &trans, const ModelComponent &model) {
        // モデルまたはメッシュがない場合はスキップ
        if (!model.modelPtr) return;

        // 全てのメッシュを影描画キューに登録
        for (const auto& mesh : model.modelPtr->GetMeshes()) {

            // 影用の伝票を作成
            ShadowCommand cmd;
            cmd.model = model.modelPtr.get();
            cmd.mesh = &mesh;
            cmd.worldMatrix = trans.worldMatrix;

            // カウンターに提出
            renderQueue.SubmitShadow(cmd);
        }
        
    });
}


REGISTER_RENDER_SYSTEM(ShadowMapRenderSystem, Priority::RenderStage::Shadow);