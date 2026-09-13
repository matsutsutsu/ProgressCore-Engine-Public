#include "ModelRenderSystem.h"

// 実装詳細で必要なインクルード
#include "Engine/Graphics/Components/Lighting/FogComponent.h"
#include "Engine/Graphics/Renderer/ModelConstantBuffers.h"
#include "Engine/Graphics/Resource/Model.h"              // Modelポインタ用
#include "Engine/Graphics/Components/Core/NoRenderTag.h" // 当たり判定専用ジオメトリの除外用
#include "Engine/Graphics/Shader/ShaderResources.h"
#include <DirectXCollision.h> // 視錐台カリング用

#include "Engine/Graphics/Renderer/RenderQueue.h"
#include "Engine/Core/Math/StringHash.h"

// システムの実行順序の定義ヘッダー
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;
using namespace DirectX;

ModelRenderSystem::ModelRenderSystem() : IfSystem("ModelRenderSystem")
{
    // 当たり判定専用ジオメトリ(NoRenderTag)は走査対象から除外する。
    //    描画ループ側に分岐を足さずに「透明な当たり判定」を実現する。
    SetExclude<NoRenderTag>();
}

void ModelRenderSystem::Update(float dt)
{

    // 最適化 シングルトンへのアクセス(ロック等のオーバーヘッド)をループの外に出す
    auto &resManager  = ResourceManager::Instance();
    auto &renderQueue = RenderQueue::Instance();

    // TransformComponent と ModelComponent の組を全エンティティ分回す
    ForEach([&](const TransformComponent  &transform,
                const ModelComponent      &modelComp,
                const MaterialComponent   &mat,
                const VisibilityComponent &vis) {
        // 最重要：ここでカリング結果を見て、画面外なら即座に処理をスキップ（サボる）
        if (!vis.isVisible) return;

        Model *model = modelComp.GetModel(); // ハンドルから取得
        if (!model) return;

        // ここには「全メッシュを走査して hasBones を求める」ループがあったが、
        //   求めた結果は一度も読まれていなかった。スキンドかどうかの判定は
        //   描画側（OpaquePass / ShadowPass）がメッシュ単位で mesh.bones を見て行う。
        //   ドローン1000機ぶん毎フレーム捨てるためだけに回っていたので削除。

        // シェーダーID決定（リストの先頭を使うなどの簡易判定）
        uint32_t shaderHash = "Phong"_hash;

        // IsValid() でチケットを確認し、実体をもらう
        // Materialを個別に持っている場合の条件
        if (!mat.overrideMaterials.empty() && mat.overrideMaterials[0].IsValid())
        {
            MaterialData *matData =
                resManager.GetMaterial(mat.overrideMaterials[0]);
            if (matData)
            {
                shaderHash = matData->shaderHash;
            }
        } // 共有マテリアルを使う場合の条件
        else
        {
            const auto &materials = model->GetMaterials();
            if (!materials.empty() && materials[0].data)
            {
                shaderHash = materials[0].data->shaderHash;
            }
        }

        // =========================================================
        // ここでは描画せず、RenderCommand を RenderQueue に積むだけ。
        // 実際のドローコールは OpaquePass 等がソート後にまとめて発行する。
        // =========================================================
        RenderCommand cmd;
        cmd.shaderHash = shaderHash;

        // ランタイム生成モデルにバッチ用の一意 identity を与える。
        //    assetHash==0 の実体ハンドルは modelHandle.index で区別する。
        //    上位ビット(0x80000000)で通常の assetHash 空間と分離
        if (modelComp.assetHash == 0 && modelComp.isRuntime &&
            modelComp.modelHandle.IsValid())
        {
            cmd.assetHash = 0x80000000u | (modelComp.modelHandle.index & 0x7FFFFFFFu);
        }
        else
        {
            cmd.assetHash = modelComp.assetHash; // ファイル由来は従来どおり
        }

        cmd.model       = model;
        cmd.worldMatrix = transform.renderWorldMatrix;

        // マテリアルオーバーライド配列のコピー
        cmd.overrideCount = static_cast<uint8_t>((std::min)(mat.overrideMaterials.size(), static_cast<size_t>(4)));
        for (uint8_t j = 0; j < cmd.overrideCount; ++j)
        {
            cmd.overrideMaterials[j] = mat.overrideMaterials[j];
        }

        cmd.customParams = mat.customParams;

        // RenderQueueのカウンターに提出！
        renderQueue.SubmitOpaque(cmd);
    });
}

REGISTER_RENDER_SYSTEM(ModelRenderSystem, Priority::RenderStage::R08_Main);