#include "CollisionSystem.h"
#include "ECS/Core/CCL_World.h"
#include "Engine/GamePlay/Graphics/Core/StaticMeshComponent.h"
#include "Game/Logics/Character/Player/PlayerComponent.h"
#include "Engine/Graphics/Renderer/ShapeRenderer.h"
#include <algorithm> // sort, unique用

// システムの実行順序の定義ヘッダー
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

// ===================================================================================
// クラス名: CollisionSystem
// 役割: 全エンティティの衝突判定を管理するシステム
//
// 処理の流れ:
//   1. 自身のAABB（占有範囲）を計算
//   2. Grid（空間分割）に問い合わせて、近くにいるEntityのリストを取得 (Broad Phase)
//   3. 取得したリストに対して詳細な衝突判定(Detect)を実行 (Narrow Phase)
//   4. 衝突していれば解決処理(Solve)を行い、位置と速度を更新する
// ===================================================================================

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

void CollisionSystem::Update(float dt)
{
    if (!_world || !_world->HasResource<Grid>()) return;
    Grid &grid = _world->GetResource<Grid>();

    // プレイヤー位置の取得（カリング基準点）
    Vector3 playerPos  = Vector3::Zero;
    bool    hasPlayer  = false;
    auto    playerView = _world->View<PlayerComponent, TransformComponent>();
    if (!playerView.empty()) {
        auto *pTrans = _world->GetComponent<TransformComponent>(playerView[0]);
        if (pTrans) {
            playerPos = pTrans->GetWorldPosition();
            hasPlayer = true;
        }
    }

    std::vector<CollisionManifold> manifolds;
    float physicsRange   = 45.0f; // 物理演算を行う範囲（AIより少し狭くしておくと安全）
    float physicsRangeSq = physicsRange * physicsRange;



    // RigidBodyを持つ（動く）エンティティをメインループで回す
    ForEachWithID([&](EntityID self, TransformComponent &transA, RigidBodyComponent &bodyA) {

        // 距離チェック
        // プレイヤーが存在し、かつ範囲外なら物理計算をスキップ
        if (hasPlayer) {
            float distSq = Vector3::DistanceSquared(transA.GetWorldPosition(), playerPos);
            if (distSq > physicsRangeSq) {
                return; // 遠すぎるので何もしない
            }
        }

        // -----------------------------------------------------------------
        // 1. フラグのリセット (Reset Flags)
        // -----------------------------------------------------------------
        // 毎フレーム「当たっていない」状態からスタートする
        ColliderWrapper myCol = CollisionHelper::GetCollider(_world, self);
        if (myCol.type != ShapeType::None) {
            if (myCol.isColliding) *myCol.isColliding = false;
            if (myCol.lastHitEntity) *myCol.lastHitEntity = CCL::ECS::InvalidEntityID;
        }

        // 静的オブジェクト同士の衝突は無視する（最適化）
        if (bodyA.isStatic) return;

        // 1. 自分のコライダ情報を取得 & AABB再計算
        // ローカル(position)ではなくワールド座標(GetWorldPosition)を使用
        Vector3      worldPos = transA.GetWorldPosition();
        Vector3      minPos   = worldPos;
        Vector3      maxPos   = worldPos;
        ColliderType typeA  = ColliderType::None;

        // 自分のレイヤー情報も事前に取得しておく
        Layer selfLayer = Layer::Default;
        Layer selfMask  = Layer::All;

        // 自分の型を特定（GetComponentの回数を最小限に）
        // ※ポインタは後でDetect内で取得するので、ここでは型とAABBだけ確定させる
        if (auto *s = _world->GetComponent<SphereColliderComponent>(self)) {
            typeA     = ColliderType::Sphere;
            float   r = s->radius;
            Vector3 ext(r, r, r);
            minPos -= ext;
            maxPos += ext;
            selfLayer = s->layer; 
            selfMask  = s->mask;  
        }
        else if (auto *b = _world->GetComponent<BoxColliderComponent>(self)) {
            typeA               = ColliderType::Box;
            Vector3 size        = Vector3(b->size.x, b->size.y, b->size.z);
            float   maxExt      = (std::max)({size.x, size.y, size.z}) * 0.5f;
            Vector3 center      = Vector3(b->center.x, b->center.y, b->center.z);
            Vector3 worldCenter = transA.position + center;
            minPos              = worldCenter - Vector3(maxExt, maxExt, maxExt);
            maxPos              = worldCenter + Vector3(maxExt, maxExt, maxExt);
            selfLayer           = b->layer; 
            selfMask            = b->mask;  
        }
        else if (auto *c = _world->GetComponent<CapsuleColliderComponent>(self)) {
            typeA               = ColliderType::Capsule;
            float   r           = c->radius;
            float   h           = c->height;
            float   maxExt      = (std::max)(r, h * 0.5f);
            Vector3 center      = Vector3(c->center.x, c->center.y, c->center.z);
            Vector3 worldCenter = transA.position + center;
            minPos              = worldCenter - Vector3(maxExt, maxExt, maxExt);
            maxPos              = worldCenter + Vector3(maxExt, maxExt, maxExt);
            selfLayer           = c->layer; 
            selfMask            = c->mask;  
        }

        if (typeA == ColliderType::None) return; // コライダなし

        // 2. Gridから近傍候補を収集 (Broad Phase)
        _neighborBuffer.clear(); // バッファリセット

        // AABB範囲のセルを走査
        float cs   = grid.cellSize;
        int   minX = (int)floor(minPos.x / cs);
        int   maxX = (int)floor(maxPos.x / cs);
        int   minY = (int)floor(minPos.y / cs);
        int   maxY = (int)floor(maxPos.y / cs);
        int   minZ = (int)floor(minPos.z / cs);
        int   maxZ = (int)floor(maxPos.z / cs);

        for (int z = minZ; z <= maxZ; ++z) {
            for (int y = minY; y <= maxY; ++y) {
                for (int x = minX; x <= maxX; ++x) {
                    // ★ラムダ式の引数に Layer targetLayer を追加
                    grid.ForEachInCell(Vector3(x * cs, y * cs, z * cs),
                        [&](EntityID targetID, ColliderType targetType, Layer targetLayer) {
                            if (self == targetID) return;

                            // =========================================================
                            // ★高速化のキモ: メモリアクセスなしで衝突判定をスキップ
                            // =========================================================
                            // 「自分が相手と当たらない」かつ「相手も自分と当たらない」なら無視
                            // 例: 弾(Bullet) vs 弾(Bullet) はここで即座に弾かれる
                            if (!CheckHit(selfMask, targetLayer) &&
                                !CheckHit(targetLayer, selfLayer)) {
                                return;
                            }

                            _neighborBuffer.push_back(targetID);
                        });
                }
            }
        }

        // 3. 候補リストの重複を排除
        // 巨大な物体は複数のセルにいるため、同じIDが何度もBufferに入っている
        if (_neighborBuffer.empty()) return;
        std::sort(_neighborBuffer.begin(), _neighborBuffer.end());
        auto last = std::unique(_neighborBuffer.begin(), _neighborBuffer.end());
        _neighborBuffer.erase(last, _neighborBuffer.end());

        // 4. 詳細判定 (Narrow Phase)
        for (EntityID targetID : _neighborBuffer) {
            // 相手の型情報をGridから取得したいが、BufferにはIDしか入れていない
            // 実はForEachInCellでpair<ID, Type>を入れるのがベストだが、
            // ここでは簡易的に「相手の型をGetComponentで特定する」か
            // 「Gridを再検索する」かの選択になる。
            // 最適化案：_neighborBufferを struct {ID, Type} のvectorにする
            // 今回はコードの単純化のため、Detect内部で相手の型を特定させる

            // 相手の型が不明なので、Detect内で全GetComponentを試すことになるが、
            // Gridを使っているので本来は型もわかっているはず。
            // ★さらに最適化するなら、bufferにTypeも保存すべきです。

            // ★修正: 「誰がチェックを担当するか」の判定ロジック
            bool shouldCheck = false;

            // パターン1: 自分が弾丸なら、必ず自分がチェックを担当する
            // (相手はグリッド内にいる敵や壁なので、相手からは自分が見えていないため)
            if (selfLayer == Layer::Bullet) {
                shouldCheck = true;
            }

            if (self < targetID) {
                // 相手のIDの方が大きい -> 自分が担当 (基本ルール)
                shouldCheck = true;
            }
            else {
                // 相手のIDの方が小さい (self > targetID)
                // 基本は相手が担当するはずだが、相手が「Static」なら担当してくれない。
                // だから相手がStaticかどうか確認する。
                auto *rbB = _world->GetComponent<RigidBodyComponent>(targetID);
                if (rbB && rbB->isStatic) {
                    shouldCheck = true; // 相手が動かないなら、動く自分(self)が計算する
                }
            }

            if (!shouldCheck) continue;

            // 簡易実装：相手の型を特定するためのヘルパー
            ColliderType typeB = ColliderType::None;
            if (_world->GetComponent<SphereColliderComponent>(targetID))
                typeB = ColliderType::Sphere;
            else if (_world->GetComponent<BoxColliderComponent>(targetID))
                typeB = ColliderType::Box;
            else if (_world->GetComponent<CapsuleColliderComponent>(targetID))
                typeB = ColliderType::Capsule;
            else if (_world->GetComponent<StaticMeshComponent>(targetID))
                typeB = ColliderType::StaticMesh; // 追加

            if (typeB == ColliderType::None) continue;

            CollisionManifold m;
            if (Detect(self, typeA, transA, targetID, typeB, m)) {
                manifolds.push_back(m);
            }
        }
    });

    // 5. 衝突解決 (Resolution)
    // 反復解決（Iteration）を導入して安定させる
    int iterations = 4; // 4回くらい回すとビシッと止まる
    for (int i = 0; i < iterations; ++i) {
        SolveCollisions(manifolds,i);
    }
}

bool CollisionSystem::Detect(EntityID entityA,
    ColliderType                      typeA,
    TransformComponent               &transA,
    EntityID                          entityB,
    ColliderType                      typeB,
    CollisionManifold                &outManifold)
{
    // コライダ情報の取得
    ColliderWrapper colA = CollisionHelper::GetCollider(_world, entityA);
    ColliderWrapper colB = CollisionHelper::GetCollider(_world, entityB);

    if (colA.type == ShapeType::None || colB.type == ShapeType::None) return false;

    // レイヤーチェック
    if (!CheckHit(*colA.mask, *colB.layer) && !CheckHit(*colB.mask, *colA.layer)) return false;

    auto *transB = _world->GetComponent<TransformComponent>(entityB);
    if (!transB) return false;

    // ---------------------------------------------------------
    // 衝突判定実行
    // ---------------------------------------------------------
    if (CollisionHelper::DetectCollision(transA, colA, *transB, colB, outManifold)) {
        outManifold.entityA = entityA;
        outManifold.entityB = entityB;

        // 衝突フラグを立てる
        if (colA.isColliding) *colA.isColliding = true;
        if (colB.isColliding) *colB.isColliding = true;

        // =========================================================
        // ★修正: 双方の lastHitEntity を賢く更新する処理
        // =========================================================

        // ヘルパーラムダ: コライダの種類を問わず、優先度付きで情報を更新する
        auto UpdateLastHit = [&](ColliderWrapper &myCol,
                                 EntityID         otherID,
                                 const Vector3   &hitNormal) {
            // 1. 相手が「静止物(Static)」かどうか調べる
            bool isOtherStatic = false;
            if (auto *rbOther = _world->GetComponent<RigidBodyComponent>(otherID)) {
                isOtherStatic = rbOther->isStatic;
            }
            else {
                // RigidBodyがない＝壁や床とみなしてStatic扱い
                isOtherStatic = true;
            }

            // 2. 現在記録されている情報と比較して「上書きすべきか」決める
            bool     shouldUpdate   = true;
            EntityID currentLastHit = CCL::ECS::InvalidEntityID;

            // コライダの種類ごとにアクセス（キャスト）
            // ※ BoxColliderには lastHitNormal が無い定義のようなので省く
            if (myCol.type == ShapeType::Sphere) {
                currentLastHit = ((SphereColliderComponent *)myCol.componentPtr)->lastHitEntity;
            }
            else if (myCol.type == ShapeType::Capsule) {
                currentLastHit = ((CapsuleColliderComponent *)myCol.componentPtr)->lastHitEntity;
            }
            else if (myCol.type == ShapeType::Box) {
                currentLastHit = ((BoxColliderComponent *)myCol.componentPtr)->lastHitEntity;
            }

            // 既に記録がある場合、優先度チェック
            if (currentLastHit != CCL::ECS::InvalidEntityID) {
                // 以前の記録相手の情報を取得
                bool isCurrentStatic = false;
                if (auto *rbCur = _world->GetComponent<RigidBodyComponent>(currentLastHit)) {
                    isCurrentStatic = rbCur->isStatic;
                }
                else {
                    isCurrentStatic = true;
                }

                // ルール: 「記録済みが動く物体(敵)」で、「新しいのが静止物(壁)」なら上書きしない
                if (!isCurrentStatic && isOtherStatic) {
                    shouldUpdate = false;
                }
            }

            // 3. 書き込み実行
            if (shouldUpdate) {
                if (myCol.type == ShapeType::Sphere) {
                    auto *s          = (SphereColliderComponent *)myCol.componentPtr;
                    s->lastHitEntity = otherID;
                    s->lastHitNormal = hitNormal;
                }
                else if (myCol.type == ShapeType::Capsule) {
                    auto *c          = (CapsuleColliderComponent *)myCol.componentPtr;
                    c->lastHitEntity = otherID;
                    c->lastHitNormal = hitNormal;
                }
                else if (myCol.type == ShapeType::Box) {
                    auto *b          = (BoxColliderComponent *)myCol.componentPtr;
                    b->lastHitEntity = otherID;
                    // Boxには lastHitNormal が無いので記録しない
                }
            }
        };

        // ★ AとBの両方に対して実行（法線の向きに注意）
        UpdateLastHit(colA, entityB, outManifold.normal);
        UpdateLastHit(colB, entityA, -outManifold.normal); // Bから見た法線は逆

        return true;
    }

    return false;
}

void CollisionSystem::SolveCollisions(
    const std::vector<CollisionManifold> &manifolds, int iterIndex)
{
    for (const auto &m : manifolds) {
        auto *transA = _world->GetComponent<TransformComponent>(m.entityA);
        auto *bodyA  = _world->GetComponent<RigidBodyComponent>(m.entityA);
        auto *transB = _world->GetComponent<TransformComponent>(m.entityB);
        auto *bodyB  = _world->GetComponent<RigidBodyComponent>(m.entityB);

        if (transA && bodyA && transB && bodyB) {

            // コライダ情報の取得（レイヤー判定用）
            ColliderWrapper colA = CollisionHelper::GetCollider(_world, m.entityA);
            ColliderWrapper colB = CollisionHelper::GetCollider(_world, m.entityB);

           // ---------------------------------------------------------
            // 2. 衝突情報の記録 (Record Collision Info)
            // ---------------------------------------------------------
            // 押し出しをするかに関わらず「当たった」という事実は記録する。
            // これにより、DamageSystem などで「isColliding == true」を検知できる。

            if (colA.isColliding) *colA.isColliding = true;
            if (colA.lastHitEntity) *colA.lastHitEntity = m.entityB;

            if (colB.isColliding) *colB.isColliding = true;
            if (colB.lastHitEntity) *colB.lastHitEntity = m.entityA;

            // ---------------------------------------------------------
            // 3. 物理挙動のフィルタリング (Skip Resolution)
            // ---------------------------------------------------------

            bool isPlayerA = (colA.layer && *colA.layer == Layer::Player);
            bool isPlayerB = (colB.layer && *colB.layer == Layer::Player);
            bool isEnemyA  = (colA.layer && *colA.layer == Layer::Enemy);
            bool isEnemyB  = (colB.layer && *colB.layer == Layer::Enemy);
            bool isBulletA = (colA.layer && *colA.layer == Layer::Bullet);
            bool isBulletB = (colB.layer && *colB.layer == Layer::Bullet);

            // パターン1: 弾丸は押し出さない
            if (isBulletA || isBulletB) continue;

 
            // 敵同士の処理 
            if (isEnemyA && isEnemyB) {
                // 物理的な跳ね返り（速度変更）は行わないので、ここで continue しますが、
                // その前に「めり込み解消（位置補正）」を行います。

                // マニホールドはキャッシュされているため、複数回ループすると
                // 毎回同じ量だけ移動してしまい、結果的に4倍の距離弾き飛ばされます。
                // そのため「最初の1回(iterIndex == 0)」だけ実行します。
                if (iterIndex == 0) {
                    // めり込んでいる深さ
                    float penetration = m.overlap;

                    // 補正率（0.2～0.8くらい。1.0だと完全に押し出すが、ジッターが出やすいので少し残すのが定石）
                    // 完全に重なりを消したい場合は 1.0f にしてください。
                    float percent = 0.8f;

                    Vector3 correction = m.normal * (penetration * percent);

                    // お互いに半分ずつずらす
                    // Static（固定砲台など）でない場合のみ動かす
                    if (!bodyA->isStatic) transA->position = transA->position + correction * 0.5f;
                    if (!bodyB->isStatic) transB->position = transB->position - correction * 0.5f;
                }

                // 物理エンジン（CollisionHelper::ResolveCollision）には渡さない
                continue;
            }


            // プレイヤーと敵の衝突なら、押し出し処理(Resolve)をスキップ
            // これにより「すり抜け」るが、「当たった事実(isColliding)」は残る
            if ((isPlayerA && isEnemyB) || (isPlayerB && isEnemyA)) {
                continue;
            }

            // ---------------------------------------------------------
            // シンプルな質量計算のみにする
            // ---------------------------------------------------------
            // プレイヤーや敵といった区別をせず、純粋にRigidBodyのmassだけで計算します。

            float invMassA = (bodyA->isStatic) ? 0.0f : (1.0f / bodyA->mass);
            float invMassB = (bodyB->isStatic) ? 0.0f : (1.0f / bodyB->mass);

            // 質量0ガード（ゼロ除算防止）
            if (!bodyA->isStatic && bodyA->mass <= 0.0001f) {
                invMassA = 1.0f;
            }
            if (!bodyB->isStatic && bodyB->mass <= 0.0001f) {
                invMassB = 1.0f;
            }

            // そのままHelperへ渡す
            CollisionHelper::ResolveCollision(
                *transA, *bodyA, *transB, *bodyB, m.normal, m.overlap, invMassA, invMassB);
        }
    }
}

void CollisionSystem::OnDrawDebug(ShapeRenderer *renderer)
{

    // 全コライダを描画するロジック
    // （SphereCollisionSystemにあったものを統合する）
    ForEachWithID([&](EntityID id, TransformComponent &t, RigidBodyComponent &r) {

        Vector3 worldPos = t.GetWorldPosition();

        // Sphere
        if (auto *s = _world->GetComponent<SphereColliderComponent>(id)) {
            Vector4 color = s->isColliding ? Vector4(1, 0, 0, 1) : Vector4(0, 1, 0, 1);
            renderer->DrawSphere(worldPos, s->radius, color);
        }
        // Box
        if (auto *b = _world->GetComponent<BoxColliderComponent>(id)) {
            Vector4 color = b->isColliding ? Vector4(1, 0, 0, 1) : Vector4(0, 1, 0, 1);
            // 本来は回転込みで描画すべき
            renderer->DrawBox(worldPos + Vector3(b->center.x, b->center.y, b->center.z),
                Vector3(0, 0, 0),
                Vector3(b->size.x, b->size.y, b->size.z),
                color);
        }
        // Capsule
        if (auto *c = _world->GetComponent<CapsuleColliderComponent>(id)) {
            Vector4 color = c->isColliding ? Vector4(1, 0, 0, 1) : Vector4(0, 1, 0, 1);

            using namespace DirectX;

            // 1. ワールド行列を取得
            XMMATRIX worldMat = XMLoadFloat4x4(&t.worldMatrix);

            // 2. スケール成分を除去（正規化）
            // これにより、Transformで拡大縮小していても、カプセルのサイズは radius/height
            // の値が厳密に守られます
            worldMat.r[0] = XMVector3Normalize(worldMat.r[0]);
            worldMat.r[1] = XMVector3Normalize(worldMat.r[1]);
            worldMat.r[2] = XMVector3Normalize(worldMat.r[2]);

            // 3. オフセット行列を作成
            XMMATRIX offsetMat = XMMatrixTranslation(c->center.x, c->center.y, c->center.z);

            // 4. 合成（オフセット(回転なし) → エンティティの回転・位置(スケールなし)）
            XMMATRIX finalMat = offsetMat * worldMat;

            XMFLOAT4X4 drawMatrix;
            XMStoreFloat4x4(&drawMatrix, finalMat);

            renderer->DrawCapsule(drawMatrix, c->radius, c->height, color);
        }
    });
}


REGISTER_LOGIC_SYSTEM(CollisionSystem, Priority::LogicStage::Collision);