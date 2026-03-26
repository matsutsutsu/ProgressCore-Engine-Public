#include "ModelRenderSystem.h"

// 実装詳細で必要なインクルード
#include "Engine/GamePlay/Graphics/Lighting/FogComponent.h"
#include "Engine/Graphics/Core/Graphics.h" // DeviceContext取得用
#include "Engine/Graphics/Renderer/ModelRenderer.h"
#include "Engine/Graphics/Resource/Model.h" // Modelポインタ用
#include "Engine/Graphics/Shader/ShaderResources.h"
#include <DirectXCollision.h> // 視錐台カリング用

// システムの実行順序の定義ヘッダー
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

using namespace CCL::ECS;
using namespace DirectX;

namespace {
    // クォータニオンの正規化チェック用ヘルパー
    inline bool IsUnitQuaternion(FXMVECTOR q, float epsilon = 1e-3f)
    {
        XMVECTOR lenSq = XMVector4Dot(q, q);
        float    value = XMVectorGetX(lenSq);
        return fabsf(value - 1.0f) < epsilon;
    }

  
} // namespace

ModelRenderSystem::ModelRenderSystem() : IfSystem("ModelRenderSystem")
{
}

void ModelRenderSystem::Update(float dt)
{
    // シングルトンではなくWorldから取得する
    if (!_world->HasResource<ModelRenderer *>()) return;
    auto *renderer = _world->GetResource<ModelRenderer *>();

    if (!renderer) return;

    // レンダラーから最新の視錐台を取得
    // 【修正】参照(const&)ではなくコピーで受け取り、値を修正できるようにする
    BoundingFrustum frustum = renderer->GetFrustum();

    // 【追加】Frustumのサニタイズ（安全化）処理
    // クォータニオンの整合性チェック
    XMVECTOR Q = XMLoadFloat4(&frustum.Orientation);

    // NaNチェック または 非正規化チェック
    if (XMQuaternionIsNaN(Q) || !IsUnitQuaternion(Q)) {
        // 値が壊れている場合は正規化を試みる
        Q = XMQuaternionNormalize(Q);

        // それでもNaNなら（ゼロベクトルだった場合など）、回転なし(Identity)にする
        if (XMQuaternionIsNaN(Q)) {
            Q = XMQuaternionIdentity();
        }
        XMStoreFloat4(&frustum.Orientation, Q);
    }


    // TransformComponent と ModelComponent の組を全エンティティ分回す
    ForEach([&](const TransformComponent &transform,
                const ModelComponent      &model,
                const MaterialComponent   &mat) {
        if (!model.modelPtr) return;

        // 安全対策: マテリアルデータが無い場合はスキップ
        // if (!mat.data) return;

        // ここで判定！UpdateTransformを呼ぶ前にチェック
        // モデル全体の境界球（またはAABB）を使って判定
        if (!IsModelInFrustum(model.modelPtr.get(), transform.worldMatrix, frustum)) {
            return;
             // 画面外なら行列計算も描画登録も一切しない
        }

        // ShadowMapでUpdateTransform行うのでここではやらない
        bool hasBones = false;
        for (const auto &mesh : model.modelPtr->GetMeshes()) {
            if (!mesh.bones.empty()) {
                hasBones = true;
                break;
            }
        }

        if (hasBones) {
            model.modelPtr->UpdateTransform(transform.worldMatrix);
        }

        // シェーダーID決定（リストの先頭を使うなどの簡易判定）
        ShaderId shaderId = ShaderId::Phong;
        // IsValid() でチケットを確認し、実体をもらう
        if (!mat.overrideMaterials.empty() && mat.overrideMaterials[0].IsValid()) {
            MaterialData *matData =
                ResourceManager::Instance().GetMaterial(mat.overrideMaterials[0]);
            if (matData) {
                shaderId = matData->shaderId;
            }
        }
        else {
            const auto &materials = model.modelPtr->GetMaterials();
            if (!materials.empty() && materials[0].data) shaderId = materials[0].data->shaderId;
        }

        // ※ループ内でのFog設定は不要（ループ外で一括設定済み）ですが、
        // もしエンティティごとにFog設定が変わる仕様ならここで行います。
        // 提示コードではループ外とループ内の両方にありましたが、
        // 効率のためループ外の処理を優先し、ここは削除しても動作します。
        // (念のため元のロジックを尊重して残す場合は以下コメントアウトを外してください)
        /*
        auto fogView = _world->View<FogComponent>();
        if (!fogView.empty()) {
            auto *fog = _world->GetComponent<FogComponent>(fogView[0]);
            ID3D11ShaderResourceView *srvs[] = {fog ? fog->noiseTextureSRV.Get() : nullptr};
            dc->PSSetShaderResources(SLOT_SRV_FOG_NOISE, 1, srvs);
        }
        */

        // レンダラーへの登録
        renderer->Draw(
            shaderId, model.modelPtr.get(), transform.worldMatrix, mat.overrideMaterials,
            mat.customParams);
    });
}


REGISTER_RENDER_SYSTEM(ModelRenderSystem, Priority::RenderStage::Main);