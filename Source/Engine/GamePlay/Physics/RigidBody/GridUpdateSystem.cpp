#include "GridUpdateSystem.h"

// 必要な機能ヘッダ
#include "ECS/Core/CCL_World.h"
#include "Engine/Graphics/Renderer/ShapeRenderer.h"
#include "Engine/Physics/Grid.h"

// 内部の実装でのみ使用するコンポーネント
#include "Engine/GamePlay/Graphics/Core/ModelComponent.h"
#include "Engine/GamePlay/Graphics/Core/StaticMeshComponent.h"
#include "Engine/GamePlay/Physics/Collision/BoxColliderComponent.h"
#include "Engine/GamePlay/Physics/Collision/CapsuleColliderComponent.h"
#include "Engine/GamePlay/Physics/Collision/SphereColliderComponent.h"

#include <SimpleMath.h>
#include <algorithm> // min, max

// 各システムの .cpp ファイルの上部に追加
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

// GridUpdateSystemでは全員の居場所を計算してグリッドに登録する役割がある
// CollisionSystemではその完成したグリッドを見て自分の近くにいるEntityを調べる

// Broad Phaseという誰がどこのエリアにいるかを大まかに仕分ける
// 流れ：
// １．GetComponentで形状を順番にチェックする
// ２．形状ごとに計算式を変えてその物体が入る最大(max)と最小(min）の座標を求める
// ３．計算した範囲（AABB）を使ってグリッドシステムに登録

// Updateの実装
void GridUpdateSystem::Update(float dt)
{
    // WorldやGridリソースのチェック
    if (!_world || !_world->HasResource<Grid>())
        return;

    Grid &grid = _world->GetResource<Grid>();
    // 1. グリッドのクリア（高速）
    grid.Clear();

    // 2. 全エンティティを走査して登録
    // ForEachWithIDを使わず、必要なコンポーネントを持つアーキタイプを対象にする方が理想だが、
    // ここでは簡易的に全Transform持ちを対象とする
    ForEachWithID([&](EntityID self, const TransformComponent &trans, const RigidBodyComponent &rb)
        {
            // NaNチェック
            if (std::isnan(trans.position.x))
                return;

            // StaticMesh判定
            if (_world->GetComponent<StaticMeshComponent>(self))
            {
                grid.staticLargeMeshes.push_back(self);
                return;
            }

            Vector3      minPos      = trans.position;     // 最小座標（左下奥）
            Vector3      maxPos      = trans.position;     // 最大座標（右上手前）
            ColliderType type        = ColliderType::None; // コライダの種類（後で使う名札）
            Layer        layer       = Layer::Default;     // レイヤー初期値
            bool         hasCollider = false;              // コライダ持ってるかフラグ

            // --- AABB計算 ---

            // 1. Sphere
            if (auto *s = _world->GetComponent<SphereColliderComponent>(self))
            {
                float   r = s->radius; // スケール考慮する場合は trans.scale.x * r など
                Vector3 extents(r, r, r);
                minPos      = trans.position - extents;
                maxPos      = trans.position + extents;
                type        = ColliderType::Sphere;
                layer       = s->layer; // Layer取得
                hasCollider = true;
            }
            // 2. Box
            else if (auto *b = _world->GetComponent<BoxColliderComponent>(self))
            {
                // 回転を考慮したAABB計算が理想だが、ここでは簡易的に最大半径で計算（またはOBBの外接AABB）
                // 簡易版: Boxのサイズ + 回転による最大拡張
                // 本来は Matrix::CreateFromQuaternion(rot) で頂点変換してMinMaxを取るべき
                Vector3 size   = Vector3(b->size.x, b->size.y, b->size.z);
                Vector3 center = Vector3(b->center.x, b->center.y, b->center.z);

                // 回転している場合、AABBは大きくなる。安全策として外接球半径を使う手もある
                // ここでは簡易的に「サイズそのままでAABB」とする（回転無視・高リスク）
                // もしくは回転後の最大幅を取る
                Vector3 worldCenter = trans.position + center; // 本来は回転が必要

                // 安全策：サイズの最大成分を半径とした球として登録しておけば、抜けはなくなる
                float maxExt = (std::max)({size.x, size.y, size.z});
                minPos       = trans.position - Vector3(maxExt, maxExt, maxExt);
                maxPos       = trans.position + Vector3(maxExt, maxExt, maxExt);

                type        = ColliderType::Box;
                layer       = b->layer; // Layer取得
                hasCollider = true;
            }
            // 3. Capsule
            else if (auto *c = _world->GetComponent<CapsuleColliderComponent>(self))
            {
                float r = c->radius;
                float h = c->height;
                // カプセルは縦長なので、Y軸方向の長さを考慮
                Vector3 extents(r, h * 0.5f, r);

                minPos      = trans.position - extents;
                maxPos      = trans.position + extents;
                type        = ColliderType::Capsule;
                layer       = c->layer; // Layer取得
                hasCollider = true;
            }

            // コライダがなければ登録しない
            if (!hasCollider)
                return;


            // ★追加: 弾丸レイヤーならグリッドに入れない！
            // これにより、グリッド内のリストが劇的に短くなります
            if (layer == Layer::Bullet) return;

            // 3. Gridへ登録 (AABB範囲全てに)
            grid.Insert(minPos, maxPos, self, type,layer);
        });
}

void GridUpdateSystem::OnDrawDebug(ShapeRenderer *renderer)
{
    // 1. ガード節：表示オフ、レンダラーなし、Gridリソースなしなら即リターン
    // これにより無駄な処理を一切行わない
    if (!this->isDebugVisible || !renderer || !_world || !_world->HasResource<Grid>())
    {
        return;
    }

    // デバッグ描画: アクティブなバケツのみ描画
    // 注意: SpatialHashは空間座標からハッシュを作るため、
    // 「どのセルが有効か」を知るには全バケツではなく「登録されたエントリ」を見る必要があるが、
    // ここではデバッグ用に簡易的な方法（あるいは描画しない）で済ませるのが無難。

    // 空間全体を走査して描画するのはコストが高すぎるため、
    // ここでは「プレイヤー周辺」や「特定のオブジェクト周辺」のセルだけ描画するなどの工夫が必要。

    // とりあえず無効化しておくか、プレイヤー位置を取得してその周辺を表示する実装に切り替えることを推奨。

    // const Grid& grid = _world->GetResource<Grid>();

    //// アクティブなセル（誰かがいるセル）を水色で描画
    // DirectX::XMFLOAT4 activeColor = { 0.0f, 1.0f, 1.0f, 1.0f }; // Cyan
    // DirectX::XMFLOAT3 angle = { 0.0f, 0.0f, 0.0f };

    //// DrawBoxのsizeは「中心からの半径（ハーフサイズ）」を指定する仕様のため半分にする
    // float halfScale = grid.cellSize * 0.5f;
    // DirectX::XMFLOAT3 boxSize = { halfScale, halfScale, halfScale };

    // for (const auto& pair : grid.cells) {
    //     const Grid::CellKey& key = pair.first; // {x, y, z}

    //    // セルの中心座標を計算
    //    float cx = (key.x + 0.5f) * grid.cellSize;
    //    float cy = (key.y + 0.5f) * grid.cellSize;
    //    float cz = (key.z + 0.5f) * grid.cellSize;

    //    renderer->DrawBox({ cx, cy, cz }, angle, boxSize, activeColor);
    //}

    //// 静的メッシュ（StaticMeshComponent持ち）を赤色で強調表示
    // DirectX::XMFLOAT4 staticColor = { 1.0f, 0.0f, 0.0f, 1.0f }; // Red
    // for (EntityID id : grid.staticLargeMeshes) {
    //     // ※GetComponentはテンプレートメソッドなのでヘッダ定義が必要だが、
    //     // .cpp内ならWorld.hが見えているので問題なく動く
    //     auto* trans = _world->GetComponent<TransformComponent>(id);
    //     if (trans) {
    //         renderer->DrawBox(trans->position, angle, { 0.5f, 0.5f, 0.5f }, staticColor);
    //     }
    // }
}

REGISTER_LOGIC_SYSTEM(GridUpdateSystem, Priority::LogicStage::PrePhysics);