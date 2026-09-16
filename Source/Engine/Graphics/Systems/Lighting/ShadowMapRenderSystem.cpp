#include "ShadowMapRenderSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Graphics/Components/Lighting/ShadowMapConfigComponent.h"
#include "Engine/Graphics/RHI/Camera.h"
#include "Engine/Graphics/RHI/Light.h"
#include "Engine/Graphics/Renderer/ModelConstantBuffers.h"
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Graphics/Components/Core/NoRenderTag.h" // 当たり判定専用ジオメトリの除外用
#include "Engine/Graphics/Components/Core/NoShadowTag.h" // 影を落とさせないものの除外用
#include "Engine/Graphics/Renderer/RenderQueue.h"
#include "Engine/Graphics/Renderer/MeshCulling.h" // メッシュ AABB 構築（Opaque/Normal と共有）
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

// ShadowMapリソースへのアクセス用
#include "Engine/Graphics/Shader/Pass/ShadowMap.h"
#include "Engine/Graphics/Shader/Pass/ShadowUtils.h"

#include <DirectXCollision.h> // BoundingBox / BoundingOrientedBox 交差判定用

using namespace CCL::ECS;
using namespace DirectX;

ShadowMapRenderSystem::ShadowMapRenderSystem()
    : IfSystem("ShadowMapRenderSystem")
{
    // 影を落とさない2種を走査対象から外す。
    //    NoRenderTag  : 当たり判定専用ジオメトリ。無いと「見えないのに影だけ出る」。
    //    NoShadowTag  : 見えるが影は落とさないもの。カスケード3枚ぶんのラスタライズが
    //                   丸ごと消えるため、大量に出る小物（ドローン等）に効く。
    // ※ SetExclude は代入なので必ず1回で列挙すること。2回呼ぶと先の指定が消える。
    SetExclude<NoRenderTag, NoShadowTag>();
}

void ShadowMapRenderSystem::Update(float dt)
{
    // シャドウマップへの描画コマンドを積む処理。
    // 実際の描画（ラスタライズ）は ShadowPass が行う。ここはあくまで
    // 「どのメッシュを・どのカスケードに描くか」を選別してキューへ積むだけ。
    RenderShadowMap();
}

// ============================================================================
// シャドウ描画コマンドの生成（提出）
//   役割：
//     1. カメラのビュー錐台を距離で 3 分割し、各カスケードのライト空間 VP 行列を作る
//     2. その VP の逆行列で「NDCの立方体 → ワールド空間の傾いた箱(OBB)」を復元し、
//        各カスケードが実際にワールド上でカバーする領域を表す OBB を 3 つ得る
//     3. 各エンティティ／各メッシュの AABB を 3 つのカスケード OBB と交差判定し、
//        「入っているカスケードだけ」を示すビットマスク(cascadeMask)を作る
//     4. どのカスケードにも入らないメッシュはキューに積まない（＝影描画から除外）
//
//   カリングの二段構え：
//     - モデル単位の早期棄却：モデル全体の AABB が全カスケード外なら、
//       メッシュを一つも調べずにエンティティごと捨てる（最速の枝刈り）
//     - メッシュ単位の絞り込み：Sponza のような「1体で数百メッシュ」のモデルは
//       モデル全体 AABB が全カスケードに掛かってしまうため、メッシュごとに
//       個別の cascadeMask を作って、遠い/視界外のメッシュを落とす
// ============================================================================
void ShadowMapRenderSystem::RenderShadowMap()
{
    // ------------------------------------------------------------------
    // 1. 必要なリソースの取得（どれか欠けていたら何もしない）
    // ------------------------------------------------------------------
    if (!m_world->HasResource<ShadowMap *>() || !m_world->HasResource<Camera *>() || !m_world->HasResource<LightManager *>()) return;

    ShadowMap    *shadowMap    = m_world->GetResource<ShadowMap *>();
    Camera       *camera       = m_world->GetResource<Camera *>();
    LightManager *lightManager = m_world->GetResource<LightManager *>();

    // ------------------------------------------------------------------
    // 2. 設定コンポーネント(SSOT)からシャドウパラメータを同期する。
    //    テンプレートのクエリ対象に縛られず、シングルトン設定だけ拾って上書き。
    // ------------------------------------------------------------------
    if (auto *config = m_world->GetSingletonComponent<ShadowMapConfigComponent>())
    {
        shadowMap->params.shadowBias       = config->shadowBias;
        shadowMap->params.shadowColor      = config->shadowColor;
        shadowMap->params.cascadeSplits[0] = config->cascadeSplits.x;
        shadowMap->params.cascadeSplits[1] = config->cascadeSplits.y;
        shadowMap->params.cascadeSplits[2] = config->cascadeSplits.z;
    }

    // ------------------------------------------------------------------
    // 3. カスケードの行列を計算し、交差判定用のOBBを3つ作成する。
    //    lightVPs[i] : カスケード i のライト空間ビュー射影行列
    // ------------------------------------------------------------------
    DirectX::XMMATRIX lightVPs[3];
    CalculateCascadeMatrices(
        DirectX::XMLoadFloat4x4(&camera->GetView()),
        camera->GetFovY(),
        camera->GetAspect(),
        lightManager->GetDirectionalLight().direction,
        shadowMap->params.cascadeSplits,
        lightVPs);

    DirectX::BoundingOrientedBox cascadeOBBs[3];
    // DirectXのNDC空間の箱 (X,Yは -1~1, Zは 0~1) => Center(0,0,0.5), Extents(1,1,0.5)
    DirectX::BoundingBox ndcBox(DirectX::XMFLOAT3(0.0f, 0.0f, 0.5f), DirectX::XMFLOAT3(1.0f, 1.0f, 0.5f));

    for (int i = 0; i < 3; ++i)
    {
        DirectX::BoundingOrientedBox::CreateFromBoundingBox(cascadeOBBs[i], ndcBox);
        // ライト行列の「逆行列」を掛けることで、NDCの立方体をワールド空間の傾いた箱(OBB)に逆変換する
        cascadeOBBs[i].Transform(cascadeOBBs[i], DirectX::XMMatrixInverse(nullptr, lightVPs[i]));
    }

    auto &renderQueue = RenderQueue::Instance();

    // メッシュのローカル AABB 構築は MeshCull::BuildLocalAABB に集約済み
    // （Opaque/Normal/Shadow で同一実装を共有し、縮退メッシュは安全側に倒れる）。

    // ------------------------------------------------------------------
    // 4. エンティティを走査してシャドウコマンドを積む
    //    （TransformComponent / ModelComponent / BoundingBoxComponent を持つもの）
    // ------------------------------------------------------------------
    ForEach([&](const TransformComponent &trans, const ModelComponent &model, const BoundingBoxComponent &bounds) {
        Model *pModel = model.GetModel();
        if (!pModel) return;

        // --- (A) モデル単位の早期棄却 ------------------------------------
        // モデル全体の worldAABB がどのカスケードにも掛からなければ、
        // メッシュを一つも調べずにこのエンティティ全体を捨てる（最速の枝刈り）。
        uint8_t modelMask = 0;
        if (cascadeOBBs[0].Intersects(bounds.worldAABB)) modelMask |= (1 << 0);
        if (cascadeOBBs[1].Intersects(bounds.worldAABB)) modelMask |= (1 << 1);
        if (cascadeOBBs[2].Intersects(bounds.worldAABB)) modelMask |= (1 << 2);
        if (modelMask == 0) return; // 全カスケード外：影に一切寄与しない

        // 基準は renderWorldMatrix（補間済み）。ModelRenderSystem が描画に使うのと同じ行列。
        //   worldMatrix（固定ステップ）を使うと、影だけが最大1固定ステップぶん
        //   本体から遅れる。BoneAttachmentRenderSystem(R04) が本システム(R06)より
        //   先に走るので、この時点で renderWorldMatrix は確定している。
        const DirectX::XMMATRIX entityWorld = DirectX::XMLoadFloat4x4(&trans.renderWorldMatrix);

        // --- (B) メッシュ単位の絞り込み ----------------------------------
        // Sponza のような巨大単一モデルは、モデル全体 AABB では全カスケードに
        // 掛かってしまう。メッシュ個別に AABB を作って cascadeMask を精密化する。
        for (const auto &mesh : pModel->GetMeshes())
        {
            if (!mesh.data) continue;

            const bool isSkinned = !mesh.bones.empty();

            uint8_t meshMask;
            if (isSkinned)
            {
                // スキンドはアニメーションで実形状がレストポーズ AABB を超えうるため、
                // メッシュ単位では絞らずモデル単位のマスクを踏襲する（影欠けを防ぐ安全側）。
                meshMask = modelMask;
            }
            else
            {
                // このメッシュのワールド行列を作る。
                // 描画時の世界変換（node->globalTransform * entityWorld）と一致させること。
                DirectX::XMMATRIX meshWorld = entityWorld;
                if (mesh.node)
                {
                    meshWorld = DirectX::XMLoadFloat4x4(&mesh.node->globalTransform) * entityWorld;
                }

                // ローカル AABB → ワールド AABB（回転を含んでも包含する軸平行ボックスになる）
                DirectX::BoundingBox worldAABB;
                MeshCull::BuildLocalAABB(mesh).Transform(worldAABB, meshWorld);

                // このメッシュ専用の cascadeMask を、各カスケード OBB との交差から作る。
                meshMask = 0;
                if (cascadeOBBs[0].Intersects(worldAABB)) meshMask |= (1 << 0);
                if (cascadeOBBs[1].Intersects(worldAABB)) meshMask |= (1 << 1);
                if (cascadeOBBs[2].Intersects(worldAABB)) meshMask |= (1 << 2);
            }

            // どのカスケードにも入らないメッシュはキューに積まない（＝影描画から除外）。
            if (meshMask == 0) continue;

            // 影用の伝票を作成
            ShadowCommand cmd;
            cmd.model = pModel;
            cmd.mesh  = &mesh;
            // worldMatrix は「エンティティのワールド行列」をそのまま積む。
            //   node の合成は ShadowPass 側で行う仕様なので、ここでは合成しない。
            //   上の entityWorld と同じ renderWorldMatrix を使うこと（判定と描画の一致）。
            cmd.worldMatrix = trans.renderWorldMatrix;
            cmd.cascadeMask = meshMask; // メッシュ個別のカスケードマスク

            renderQueue.SubmitShadow(cmd);
        }
    });
}

REGISTER_RENDER_SYSTEM(ShadowMapRenderSystem, Priority::RenderStage::R06_Shadow);