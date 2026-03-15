#include "CollisionHelper.h"
#include "ECS/Core/CCL_World.h"

using namespace DirectX::SimpleMath;

// ★ エンティティが持っているコライダを探して、統一フォーマット(Wrapper)で返す
ColliderWrapper CollisionHelper::GetCollider(CCL::ECS::Core::World *world, CCL::ECS::EntityID id)
{
    ColliderWrapper wrapper;
    wrapper.type = ShapeType::None;

    // 1. Sphere確認
    if (auto *s = world->GetComponent<SphereColliderComponent>(id))
    {
        wrapper.type          = ShapeType::Sphere;
        wrapper.componentPtr  = s;
        wrapper.layer         = &s->layer;
        wrapper.mask          = &s->mask;
        wrapper.isColliding   = &s->isColliding;
        wrapper.lastHitEntity = &s->lastHitEntity;
        wrapper.lastHitNormal = &s->lastHitNormal;
        return wrapper;
    }

    // 2. Box確認
    if (auto *b = world->GetComponent<BoxColliderComponent>(id))
    {
        wrapper.type          = ShapeType::Box;
        wrapper.componentPtr  = b;
        wrapper.layer         = &b->layer;
        wrapper.mask          = &b->mask;
        wrapper.isColliding   = &b->isColliding;
        wrapper.lastHitEntity = &b->lastHitEntity;
        // BoxはHitNormalを持っていないのでnullptrのまま
        return wrapper;
    }

    // 3. Capsule確認
    if (auto *c = world->GetComponent<CapsuleColliderComponent>(id))
    {
        wrapper.type          = ShapeType::Capsule;
        wrapper.componentPtr  = c;
        wrapper.layer         = &c->layer;
        wrapper.mask          = &c->mask;
        wrapper.isColliding   = &c->isColliding;
        wrapper.lastHitEntity = &c->lastHitEntity;
        wrapper.lastHitNormal = &c->lastHitNormal;
        return wrapper;
    }

    return wrapper; // 何も持っていない
}

// ★ 形状の組み合わせに応じて適切な計算関数を呼ぶ
bool CollisionHelper::DetectCollision(const TransformComponent &transA,
    const ColliderWrapper                                      &colA,
    const TransformComponent                                   &transB,
    const ColliderWrapper                                      &colB,
    CollisionManifold                                          &outManifold)
{
    ShapeType typeA = colA.type;
    ShapeType typeB = colB.type;

    // ---------------------------------------------------------
    // Case A: Sphere vs ...
    // ---------------------------------------------------------
    if (typeA == ShapeType::Sphere)
    {
        auto *sphereA = static_cast<SphereColliderComponent *>(colA.componentPtr);

        if (typeB == ShapeType::Sphere)
        {
            auto *sphereB = static_cast<SphereColliderComponent *>(colB.componentPtr);
            return CheckSphereVsSphere(transA, *sphereA, transB, *sphereB, outManifold);
        }
        else if (typeB == ShapeType::Box)
        {
            auto *boxB = static_cast<BoxColliderComponent *>(colB.componentPtr);
            return CheckSphereVsBox(transA, *sphereA, transB, *boxB, outManifold);
        }
        else if (typeB == ShapeType::Capsule)
        {
            auto *capB = static_cast<CapsuleColliderComponent *>(colB.componentPtr);
            return CheckSphereVsCapsule(transA, *sphereA, transB, *capB, outManifold);
        }
    }

    // ---------------------------------------------------------
    // Case B: Box vs ...
    // ---------------------------------------------------------
    else if (typeA == ShapeType::Box)
    {
        auto *boxA = static_cast<BoxColliderComponent *>(colA.componentPtr);

        if (typeB == ShapeType::Sphere)
        {
            auto *sphereB = static_cast<SphereColliderComponent *>(colB.componentPtr);
            bool  hit     = CheckSphereVsBox(transB, *sphereB, transA, *boxA, outManifold);
            if (hit)
                outManifold.normal *= -1.0f;
            return hit;
        }
        else if (typeB == ShapeType::Box)
        {
            auto *boxB = static_cast<BoxColliderComponent *>(colB.componentPtr);
            return CheckBoxVsBox(transA, *boxA, transB, *boxB, outManifold);
        }
        else if (typeB == ShapeType::Capsule) // ★追加: Box vs Capsule
        {
            auto *capB = static_cast<CapsuleColliderComponent *>(colB.componentPtr);
            // Capsule vs Box を呼んで法線を反転させる
            bool hit = CheckCapsuleVsBox(transB, *capB, transA, *boxA, outManifold);
            if (hit)
                outManifold.normal *= -1.0f;
            return hit;
        }
    }
    // ---------------------------------------------------------
    // Case C: Capsule vs ...
    // ---------------------------------------------------------
    else if (typeA == ShapeType::Capsule)
    {
        auto *capA = static_cast<CapsuleColliderComponent *>(colA.componentPtr);

        if (typeB == ShapeType::Sphere)
        {
            auto *sphereB = static_cast<SphereColliderComponent *>(colB.componentPtr);
            bool  hit     = CheckSphereVsCapsule(transB, *sphereB, transA, *capA, outManifold);
            if (hit)
                outManifold.normal *= -1.0f;
            return hit;
        }
        else if (typeB == ShapeType::Box) // Capsule vs Box
        {
            auto *boxB = static_cast<BoxColliderComponent *>(colB.componentPtr);
            return CheckCapsuleVsBox(transA, *capA, transB, *boxB, outManifold);
        }
        else if (typeB == ShapeType::Capsule) // Capsule vs Capsule
        {
            auto *capB = static_cast<CapsuleColliderComponent *>(colB.componentPtr);
            return CheckCapsuleVsCapsule(transA, *capA, transB, *capB, outManifold);
        }
    }

    return false;
}

bool CollisionHelper::CheckSphereVsSphere(const TransformComponent &transA,
    const SphereColliderComponent                                  &sphereA,
    const TransformComponent                                       &transB,
    const SphereColliderComponent                                  &sphereB,
    CollisionManifold                                              &outManifold)
{
    Vector3 diff      = transB.position - transA.position;
    float   distSq    = diff.LengthSquared();
    float   radiusSum = sphereA.radius + sphereB.radius;

    if (distSq < radiusSum * radiusSum)
    {
        float   dist    = sqrtf(distSq);
        Vector3 normal  = (dist > 0.0001f) ? diff / dist : Vector3(0, 1, 0);
        float   overlap = radiusSum - dist;

        outManifold.normal  = normal;
        outManifold.overlap = overlap;
        return true;
    }
    return false;
}

bool CollisionHelper::CheckSphereVsBox(const TransformComponent &transSphere,
    const SphereColliderComponent                               &sphere,
    const TransformComponent                                    &transBox,
    const BoxColliderComponent                                  &box,
    CollisionManifold                                           &outManifold)
{
    // 1. 球の中心を、箱のローカル座標系に変換する
    // (箱の逆行列を掛けることで、箱を原点・無回転の状態として扱える)
    Matrix boxWorld = Matrix::CreateScale(transBox.scale) *
                      Matrix::CreateFromQuaternion(transBox.rotation) *
                      Matrix::CreateTranslation(transBox.position);
    Matrix boxInverse = boxWorld.Invert();

    Vector3 sphereWorldPos = transSphere.position;
    Vector3 sphereLocalPos = Vector3::Transform(sphereWorldPos, boxInverse);

    // BoxColliderのCenterオフセットを考慮
    // (BoxColliderComponent.center がローカルオフセットの場合)
    Vector3 boxLocalCenter = Vector3(box.center.x, box.center.y, box.center.z);
    Vector3 targetLocalPos = sphereLocalPos - boxLocalCenter;

    // 2. AABB (Axis Aligned Bounding Box) として最近接点を求める
    Vector3 halfExtents = Vector3(box.size.x, box.size.y, box.size.z);
    Vector3 closestLocalPoint;
    closestLocalPoint.x = std::clamp(targetLocalPos.x, -halfExtents.x, halfExtents.x);
    closestLocalPoint.y = std::clamp(targetLocalPos.y, -halfExtents.y, halfExtents.y);
    closestLocalPoint.z = std::clamp(targetLocalPos.z, -halfExtents.z, halfExtents.z);

    // 3. 距離チェック
    Vector3 localDiff = targetLocalPos - closestLocalPoint;
    float   distSq    = localDiff.LengthSquared();

    if (distSq < sphere.radius * sphere.radius)
    {
        float dist = sqrtf(distSq);

        // ローカルでの法線
        Vector3 localNormal = (dist > 0.0001f) ? localDiff / dist : Vector3(0, 1, 0);
        float   overlap     = sphere.radius - dist;

        // 4. ワールド座標に戻す
        // 法線は回転のみ適用（平行移動は無視）
        Vector3 worldNormal = Vector3::TransformNormal(localNormal, boxWorld);
        worldNormal.Normalize();

        // 球(A)から箱(B)へのベクトルが欲しいので反転させる必要があるかも確認
        // ここでは「箱表面(Closest) -> 球中心」の向きになっているため、
        // 「球(A) -> 箱(B)」なら反転が必要。
        // SphereCollisionSystemの仕様に合わせて A(Sphere) -> B(Box) のNormalを返す
        outManifold.normal  = -worldNormal;
        outManifold.overlap = overlap;

        return true;
    }
    return false;
}

// 線分 ab と 点 p の距離計算
float CollisionHelper::DistSqPointSegment(
    const Vector3 &a, const Vector3 &b, const Vector3 &p, Vector3 &outClosestPoint)
{
    Vector3 ab = b - a;
    Vector3 ap = p - a;

    // abの長さの二乗
    float t = ap.Dot(ab);
    float d = ab.LengthSquared();

    // 線分がつぶれている（長さゼロ）の場合の保護
    if (d <= 0.0001f)
    {
        outClosestPoint = a;
        return ap.LengthSquared();
    }

    // tを正規化 (0.0 ～ 1.0 の範囲に収める)
    t = std::clamp(t / d, 0.0f, 1.0f);

    // 最短点
    outClosestPoint = a + ab * t;

    // 距離の二乗を返す
    return Vector3::DistanceSquared(p, outClosestPoint);
}

bool CollisionHelper::CheckSphereVsCapsule(const TransformComponent &transSphere,
    const SphereColliderComponent                                   &sphere,
    const TransformComponent                                        &transCapsule,
    const CapsuleColliderComponent                                  &capsule,
    CollisionManifold                                               &outManifold)
{
    // 1. カプセルの線分の始点(A)と終点(B)をワールド座標で求める
    // カプセルはY軸方向。全長 height のうち、上下の半球部分を除いた「円柱部分」が線分になる。
    // 線分の長さ = height - 2 * radius
    // ただし height < 2*radius の場合はただの球として扱うなどのガードが必要だが、ここでは簡易化。

    float segmentHalfHeight = (capsule.height * 0.5f) - capsule.radius;
    if (segmentHalfHeight < 0)
        segmentHalfHeight = 0;

    // ローカルでの上点と下点
    Vector3 localTop =
        Vector3(capsule.center.x, capsule.center.y + segmentHalfHeight, capsule.center.z);
    Vector3 localBottom =
        Vector3(capsule.center.x, capsule.center.y - segmentHalfHeight, capsule.center.z);

    // ワールド変換（回転対応）
    Matrix mat = Matrix::CreateFromQuaternion(transCapsule.rotation) *
                 Matrix::CreateTranslation(transCapsule.position);

    Vector3 worldTop    = Vector3::Transform(localTop, mat);
    Vector3 worldBottom = Vector3::Transform(localBottom, mat);

    // 2. 「球の中心」と「カプセルの線分」の最短距離を求める
    Vector3 closestPointOnSegment;
    float   distSq =
        DistSqPointSegment(worldBottom, worldTop, transSphere.position, closestPointOnSegment);

    // 3. 判定
    // 実質的に「球(sphere.radius)」と「線分上の点にある球(capsule.radius)」の衝突と同じ
    float radiusSum = sphere.radius + capsule.radius;

    if (distSq < radiusSum * radiusSum)
    {
        float dist = sqrtf(distSq);

        // 法線： 球の中心 - 線分上の最近点
        Vector3 normal = transSphere.position - closestPointOnSegment;
        if (dist > 0.0001f)
        {
            normal /= dist; // 正規化
        }
        else
        {
            normal = Vector3::Up; // 完全に重なった場合
        }

        outManifold.normal  = normal;
        outManifold.overlap = radiusSum - dist;
        return true;
    }

    return false;
}

void CollisionHelper::ResolveCollision(TransformComponent &transA,
    RigidBodyComponent                                    &bodyA,
    TransformComponent                                    &transB,
    RigidBodyComponent                                    &bodyB,
    const Vector3                                         &normal,
    float                                                  overlap,
    float                                                  invMassA,
    float                                                  invMassB)
{
    float totalInvMass = invMassA + invMassB;

    // 安全装置: 両方動かない(質量無限大)なら何もしない
    if (totalInvMass <= 0.00001f) return;

    // ==================================================================
    // 1. 位置の押し出し (Weighted Positional Correction)
    // ==================================================================

    // 完全に重なった場合(normalが0)の安全対策
    // これがないと、座標が一致した瞬間に計算不能(NaN)になりワープします
    Vector3 safeNormal = normal;
    if (safeNormal.LengthSquared() < 0.0001f) {
        safeNormal = Vector3(0, 1, 0); // 予備の押し出し方向（上）
    }

    const float k_slop = 0.01f;

    // ★修正1: 補正率を0.8 (80%) に設定
    // 強く押し出します。合体防止のためには高い値が必要です。
    const float k_percent = 0.8f;

    // めり込み解消量
    float   correctionMag = (std::max)(overlap - k_slop, 0.0f) / totalInvMass * k_percent;
    Vector3 correction    = safeNormal * correctionMag;

    // ★修正2: MAX_CORRECTION (リミッター) を削除！
    // 敵のスピードが速い場合、リミッターがあると押し出しが間に合わず合体します。
    // リミッターを外すことで、どんなに深くめり込んでも必ず外へ弾き出します。

    // 座標更新 (Vector3変換付き)
    if (!bodyA.isStatic) {
        Vector3 posA = transA.position;
        posA -= correction * invMassA;
        transA.position = posA;
    }
    if (!bodyB.isStatic) {
        Vector3 posB = transB.position;
        posB += correction * invMassB;
        transB.position = posB;
    }

    // ==================================================================
    // 2. 速度の解決 (Velocity Resolution)
    // ==================================================================
    Vector3 velA = bodyA.velocity;
    Vector3 velB = bodyB.velocity;
    Vector3 rv   = velB - velA; // 相対速度

    float velAlongNormal = rv.Dot(safeNormal);

    // すでに離れようとしているなら速度はいじらない
    if (velAlongNormal > 0) return;

    // 反発係数 (0.0推奨)
    float e = 0.0f;

    float j = -(1.0f + e) * velAlongNormal;
    j /= totalInvMass;

    Vector3 impulse = safeNormal * j;

    // 速度の更新 (Vector3変換付き)
    if (!bodyA.isStatic) {
        Vector3 newVelA = Vector3(bodyA.velocity);
        newVelA -= impulse * invMassA;
        bodyA.velocity = newVelA;
    }
    if (!bodyB.isStatic) {
        Vector3 newVelB = Vector3(bodyB.velocity);
        newVelB += impulse * invMassB;
        bodyB.velocity = newVelB;
    }

    // ==================================================================
    // 3. 摩擦 (Friction)
    // ==================================================================
    velA = bodyA.velocity;
    velB = bodyB.velocity;
    rv   = velB - velA;

    Vector3 tangent    = rv - (safeNormal * rv.Dot(safeNormal));
    float   tangentLen = tangent.Length();

    if (tangentLen > 0.0001f) {
        tangent /= tangentLen;

        float jt = -rv.Dot(tangent);
        jt /= totalInvMass;

        float mu          = (bodyA.dynamicFriction + bodyB.dynamicFriction) * 0.5f;
        float maxFriction = std::abs(j) * mu;

        Vector3 frictionImpulse;
        if (std::abs(jt) < maxFriction) {
            frictionImpulse = tangent * jt;
        }
        else {
            frictionImpulse = tangent * ((jt > 0 ? maxFriction : -maxFriction));
        }

        if (!bodyA.isStatic) {
            Vector3 finalVelA = Vector3(bodyA.velocity);
            finalVelA -= frictionImpulse * invMassA;
            bodyA.velocity = finalVelA;
        }
        if (!bodyB.isStatic) {
            Vector3 finalVelB = Vector3(bodyB.velocity);
            finalVelB += frictionImpulse * invMassB;
            bodyB.velocity = finalVelB;
        }
    }
}

// SAT (Separating Axis Theorem) のためのヘルパー
// 軸 axis に対して、OBB を投影したときの [min, max] を求める
void GetSpan(const DirectX::SimpleMath::Vector3 &axis,
    const DirectX::SimpleMath::Vector3          &center,
    const DirectX::SimpleMath::Vector3          *axisList,
    const DirectX::SimpleMath::Vector3          &halfExtents,
    float                                       &minVal,
    float                                       &maxVal)
{
    using namespace DirectX::SimpleMath;
    // 中心点の投影
    float p = axis.Dot(center);

    // 半径の投影（各軸の成分を投影して絶対値の和を取る）
    float r = std::abs(axis.Dot(axisList[0])) * halfExtents.x +
              std::abs(axis.Dot(axisList[1])) * halfExtents.y +
              std::abs(axis.Dot(axisList[2])) * halfExtents.z;

    minVal = p - r;
    maxVal = p + r;
}

// 2つのスパンの重なりを計算する
float GetOverlap(float minA, float maxA, float minB, float maxB)
{
    if (maxA < minB || maxB < minA)
        return 0.0f; // 重なっていない

    // 重なり量を返す
    float d1 = maxA - minB;
    float d2 = maxB - minA;
    return (d1 < d2) ? d1 : -d2; // 小さい方を返す（符号で方向もわかる）
    // ※今回は絶対値の小さい方を採用するロジックにするため、単純な差分で返す
}

bool CollisionHelper::CheckBoxVsBox(const TransformComponent &transA,
    const BoxColliderComponent                               &boxA,
    const TransformComponent                                 &transB,
    const BoxColliderComponent                               &boxB,
    CollisionManifold                                        &outManifold)
{
    using namespace DirectX::SimpleMath;

    // 1. 各Boxのワールド変換行列を作成
    Matrix matA = Matrix::CreateScale(transA.scale) *
                  Matrix::CreateFromQuaternion(transA.rotation) *
                  Matrix::CreateTranslation(transA.position);
    Matrix matB = Matrix::CreateScale(transB.scale) *
                  Matrix::CreateFromQuaternion(transB.rotation) *
                  Matrix::CreateTranslation(transB.position);

    // 2. 中心と軸、半サイズを取得
    Vector3 centerA = Vector3::Transform(boxA.center, matA);
    Vector3 centerB = Vector3::Transform(boxB.center, matB);

    // 回転行列部分から軸（XYZ）を抽出
    // TransformNormal で (1,0,0) 等を変換すると回転後の軸が得られる
    // ※Scaleが含まれていると軸の長さが変わるので注意。今回は正規化する。
    Vector3 axesA[3];
    axesA[0] = Vector3::TransformNormal(Vector3::UnitX, matA);
    axesA[0].Normalize();
    axesA[1] = Vector3::TransformNormal(Vector3::UnitY, matA);
    axesA[1].Normalize();
    axesA[2] = Vector3::TransformNormal(Vector3::UnitZ, matA);
    axesA[2].Normalize();

    Vector3 axesB[3];
    axesB[0] = Vector3::TransformNormal(Vector3::UnitX, matB);
    axesB[0].Normalize();
    axesB[1] = Vector3::TransformNormal(Vector3::UnitY, matB);
    axesB[1].Normalize();
    axesB[2] = Vector3::TransformNormal(Vector3::UnitZ, matB);
    axesB[2].Normalize();

    // BoxCollider.size は全幅なので、半幅にする (*
    // Scaleも考慮が必要だが、Matrixで変換済み座標系で考えるなら...)
    // ここでは「ワールド空間での軸」と「ローカルでの半幅 * スケール」で計算する方式をとる。
    // trans.scale が Matrix に入っているので、BoxCollider.size * 0.5 だけ使えば良いか？
    // -> axesを正規化したので、halfExtents側でスケールを掛ける必要がある。
    // 簡易的に： (Size * 0.5) * Scale
    Vector3 halfA = boxA.size * 0.5f * transA.scale;
    Vector3 halfB = boxB.size * 0.5f * transB.scale;

    // 3. SAT (分離軸判定)
    // テストすべき軸は 15本 (Aの3軸 + Bの3軸 + 9個の外積軸)

    Vector3 testAxes[15];
    int     axisCount = 0;

    // Aの3軸
    for (int i = 0; i < 3; ++i)
        testAxes[axisCount++] = axesA[i];
    // Bの3軸
    for (int i = 0; i < 3; ++i)
        testAxes[axisCount++] = axesB[i];
    // 外積軸 (Edge vs Edge)
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            Vector3 cross = axesA[i].Cross(axesB[j]);
            if (cross.LengthSquared() > 0.001f)
            { // 平行でない場合のみ
                cross.Normalize();
                testAxes[axisCount++] = cross;
            }
        }
    }

    float   minOverlap = FLT_MAX;
    Vector3 bestAxis;

    for (int i = 0; i < axisCount; ++i)
    {
        Vector3 axis = testAxes[i];
        if (axis.LengthSquared() < 0.001f)
            continue;

        float minA, maxA, minB, maxB;
        GetSpan(axis, centerA, axesA, halfA, minA, maxA);
        GetSpan(axis, centerB, axesB, halfB, minB, maxB);

        // 重なりをチェック
        float overlap1 = maxA - minB;
        float overlap2 = maxB - minA;

        if (overlap1 < 0 || overlap2 < 0)
        {
            return false; // 分離軸が見つかった＝衝突していない
        }

        // 小さい方の重なりを採用
        float overlap = (overlap1 < overlap2) ? overlap1 : overlap2;

        // 最小の重なりなら記録 (これが押し出しベクトルになる)
        if (overlap < minOverlap)
        {
            minOverlap = overlap;
            bestAxis   = axis;

            // 軸の向きを A -> B に補正
            // 中心間のベクトルと軸の内積を見て、逆なら反転
            Vector3 dir = centerB - centerA;
            if (dir.Dot(bestAxis) < 0)
            {
                bestAxis = -bestAxis;
            }
        }
    }

    // ここまで来たら衝突している
    outManifold.normal  = bestAxis;
    outManifold.overlap = minOverlap;

    return true;
}

// 2つの線分 (p1-q1) と (p2-q2) の間の最短距離を求める
float CollisionHelper::DistSegmentSegment(const Vector3 &p1,
    const Vector3                                       &q1,
    const Vector3                                       &p2,
    const Vector3                                       &q2,
    Vector3                                             &outC1,
    Vector3                                             &outC2)
{
    Vector3 d1 = q1 - p1; // 線分1の方向
    Vector3 d2 = q2 - p2; // 線分2の方向
    Vector3 r  = p1 - p2;
    float   a  = d1.Dot(d1); // 線分1の長さの二乗
    float   e  = d2.Dot(d2); // 線分2の長さの二乗
    float   f  = d2.Dot(r);

    const float EPSILON = 0.0001f;

    float s, t; // パラメータ (0~1)

    // 両方の線分が点でないかチェック
    if (a <= EPSILON && e <= EPSILON)
    {
        // 両方点
        s = t = 0.0f;
        outC1 = p1;
        outC2 = p2;
        return Vector3::Distance(outC1, outC2);
    }
    if (a <= EPSILON)
    {
        // 線分1が点
        s = 0.0f;
        t = std::clamp(f / e, 0.0f, 1.0f);
    }
    else
    {
        float c = d1.Dot(r);
        if (e <= EPSILON)
        {
            // 線分2が点
            t = 0.0f;
            s = std::clamp(-c / a, 0.0f, 1.0f);
        }
        else
        {
            // 一般的なケース
            float b     = d1.Dot(d2);
            float denom = a * e - b * b; // 行列式

            if (denom != 0.0f)
            {
                s = std::clamp((b * f - c * e) / denom, 0.0f, 1.0f);
            }
            else
            {
                // 平行な場合
                s = 0.0f;
            }

            t = (b * s + f) / e;

            if (t < 0.0f)
            {
                t = 0.0f;
                s = std::clamp(-c / a, 0.0f, 1.0f);
            }
            else if (t > 1.0f)
            {
                t = 1.0f;
                s = std::clamp((b - c) / a, 0.0f, 1.0f);
            }
        }
    }

    outC1 = p1 + d1 * s;
    outC2 = p2 + d2 * t;
    return Vector3::Distance(outC1, outC2);
}


bool CollisionHelper::CheckCapsuleVsBox(const TransformComponent &transCap,
    const CapsuleColliderComponent                               &cap,
    const TransformComponent                                     &transBox,
    const BoxColliderComponent                                   &box,
    CollisionManifold                                            &outManifold)
{
    // -----------------------------------------------------
    // 1. カプセル情報を取得 (線分 + 半径)
    // -----------------------------------------------------
    float halfH = (std::max)(0.0f, cap.height * 0.5f - cap.radius);
    // ローカル座標系での線分始点・終点
    Vector3 capP0_L(cap.center.x, cap.center.y - halfH, cap.center.z);
    Vector3 capP1_L(cap.center.x, cap.center.y + halfH, cap.center.z);

    Matrix matCap = Matrix::CreateFromQuaternion(transCap.rotation) *
                    Matrix::CreateTranslation(transCap.position);
    Vector3 capP0   = Vector3::Transform(capP0_L, matCap);
    Vector3 capP1   = Vector3::Transform(capP1_L, matCap);
    Vector3 capAxis = capP1 - capP0;
    float   capLen  = capAxis.Length();
    if (capLen > 1e-5f)
        capAxis /= capLen;
    else
        capAxis = Vector3::Up; // 点に近い場合

    // -----------------------------------------------------
    // 2. Box情報を取得 (OBB)
    // -----------------------------------------------------
    Matrix matBox = Matrix::CreateScale(transBox.scale) *
                    Matrix::CreateFromQuaternion(transBox.rotation) *
                    Matrix::CreateTranslation(transBox.position);

    // Boxの中心と軸
    Vector3 boxCenter = Vector3::Transform(box.center, matBox);
    Vector3 boxAxes[3];
    boxAxes[0] = Vector3::TransformNormal(Vector3::UnitX, matBox);
    boxAxes[0].Normalize();
    boxAxes[1] = Vector3::TransformNormal(Vector3::UnitY, matBox);
    boxAxes[1].Normalize();
    boxAxes[2] = Vector3::TransformNormal(Vector3::UnitZ, matBox);
    boxAxes[2].Normalize();

    // Boxの半径 (Half Extents)
    Vector3 boxHalf = box.size  * transBox.scale;

    // -----------------------------------------------------
    // 3. SAT (分離軸判定)
    // テストする軸:
    //  - Boxの3軸 (面との衝突)
    //  - (Box軸 x カプセル軸) の3軸 (エッジとの衝突)
    // -----------------------------------------------------

    Vector3 testAxes[6];
    int     axisCount = 0;

    // Boxの3軸
    for (int i = 0; i < 3; ++i)
        testAxes[axisCount++] = boxAxes[i];

    // エッジ判定軸 (BoxAxis x CapAxis)
    for (int i = 0; i < 3; ++i)
    {
        Vector3 cross = boxAxes[i].Cross(capAxis);
        if (cross.LengthSquared() > 0.001f)
        {
            cross.Normalize();
            testAxes[axisCount++] = cross;
        }
    }

    float   minOverlap = FLT_MAX;
    Vector3 bestAxis;

    for (int i = 0; i < axisCount; ++i)
    {
        Vector3 axis = testAxes[i];
        if (axis.LengthSquared() < 0.001f)
            continue;

        // --- Boxの投影 ---
        // Boxは中心からの半径射影
        float rBox = std::abs(axis.Dot(boxAxes[0])) * boxHalf.x +
                     std::abs(axis.Dot(boxAxes[1])) * boxHalf.y +
                     std::abs(axis.Dot(boxAxes[2])) * boxHalf.z;
        float pBox   = axis.Dot(boxCenter);
        float minBox = pBox - rBox;
        float maxBox = pBox + rBox;

        // --- Capsuleの投影 ---
        // 線分の2点(P0, P1)を投影し、さらに半径(radius)分を広げる
        float pCap0  = axis.Dot(capP0);
        float pCap1  = axis.Dot(capP1);
        float minCap = (std::min)(pCap0, pCap1) - cap.radius;
        float maxCap = (std::max)(pCap0, pCap1) + cap.radius;

        // --- 重なり判定 ---
        float overlap = (std::min)(maxBox, maxCap) - (std::max)(minBox, minCap);

        if (overlap < 0)
            return false; // 分離軸発見＝衝突なし

        if (overlap < minOverlap)
        {
            minOverlap = overlap;
            bestAxis   = axis;
            // 向き補正 (Capsule -> Box)
            // ここではCapsuleをA、BoxをBとしているため、A->Bの向きにする
            if (axis.Dot(boxCenter - ((capP0 + capP1) * 0.5f)) < 0)
            {
                bestAxis = -bestAxis;
            }
        }
    }

    outManifold.normal  = bestAxis;
    outManifold.overlap = minOverlap;
    return true;
}

bool CollisionHelper::CheckCapsuleVsCapsule(const TransformComponent &transA,
    const CapsuleColliderComponent                                   &capA,
    const TransformComponent                                         &transB,
    const CapsuleColliderComponent                                   &capB,
    CollisionManifold                                                &outManifold)
{
    // Aの線分計算
    float   hA = (std::max)(0.0f, capA.height * 0.5f - capA.radius);
    Vector3 topA_L(capA.center.x, capA.center.y + hA, capA.center.z);
    Vector3 btmA_L(capA.center.x, capA.center.y - hA, capA.center.z);
    Matrix  matA =
        Matrix::CreateFromQuaternion(transA.rotation) * Matrix::CreateTranslation(transA.position);
    Vector3 topA = Vector3::Transform(topA_L, matA);
    Vector3 btmA = Vector3::Transform(btmA_L, matA);

    // Bの線分計算
    float   hB = (std::max)(0.0f, capB.height * 0.5f - capB.radius);
    Vector3 topB_L(capB.center.x, capB.center.y + hB, capB.center.z);
    Vector3 btmB_L(capB.center.x, capB.center.y - hB, capB.center.z);
    Matrix  matB =
        Matrix::CreateFromQuaternion(transB.rotation) * Matrix::CreateTranslation(transB.position);
    Vector3 topB = Vector3::Transform(topB_L, matB);
    Vector3 btmB = Vector3::Transform(btmB_L, matB);

    // 線分間の最短点と距離を計算
    Vector3 cA, cB;
    float   dist      = DistSegmentSegment(btmA, topA, btmB, topB, cA, cB);
    float   radiusSum = capA.radius + capB.radius;

    if (dist < radiusSum)
    {
        Vector3 normal = cA - cB;
        if (dist > 0.0001f)
        {
            normal /= dist;
        }
        else
        {
            // 完全に重なった場合、Y軸などを適当な軸に取る
            normal = Vector3::Up;
        }

        outManifold.normal  = normal;
        outManifold.overlap = radiusSum - dist;
        return true;
    }

    return false;
}