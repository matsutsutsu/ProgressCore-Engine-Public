#include "PhysicsIntegrationSystem.h"
#include "Engine/Physics/RaycastUtil.h"

// 各システムの .cpp ファイルの上部に追加
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

// ===================================================================================
// ファイル名: PhysicsIntegrationSystem.cpp
// クラス名: PhysicsIntegrationSystem
//
// 【役割・責任】
//   ・物理シミュレーションの最終工程「移動（積分）」を担当する
//   ・速度(Velocity)に基づいて座標(Position)を更新する
//   ・「壁ずり（Wall Slide）」処理を行う
//
// 【仕様】
//   ・処理順序:
//     1. 重力適用（Gravity）
//     2. 摩擦適用（Friction）
//     3. 移動量算出（Delta = Velocity * dt）
//     4. 壁判定と補正（Map Collision）
//        - X軸移動 -> 壁判定 -> 当たるならX移動キャンセル
//        - Z軸移動 -> 壁判定 -> 当たるならZ移動キャンセル
//        - これにより、壁に向かって斜めに進んだ際に「沿って滑る」挙動を実現する
//     5. 座標更新
//
//   ・依存リソース: StageMap（存在しない場合は通常の物理挙動のみ行う）
// ===================================================================================

// SimpleMathを使いやすくする
using namespace DirectX::SimpleMath;

void PhysicsIntegrationSystem::Update(float dt) {
    using namespace CCL::ECS;

    // マップリソースの取得（なくても動くようにポインタで管理）
    bool            hasMap = _world->HasResource<StageMap>();
    const StageMap *map    = hasMap ? &_world->GetResource<StageMap>() : nullptr;

    ForEachParallel([&](TransformComponent &trans, RigidBodyComponent &rb) {
        // 静的オブジェクト（壁など）は物理計算しない
        if (rb.isStatic)
            return;

        // ---------------------------------------------------
        // 1. 外力の適用 (重力・摩擦)
        // ---------------------------------------------------

        // 重力
        if (rb.useGravity && !rb.isGrounded) {
            rb.velocity.y -= 9.8f * dt;
        }

        // ★修正: 摩擦計算 (Friction)
        // 引き算(ブレーキ)だと押し合い時に「くっつく」原因になるため、
        // 掛け算(減衰)に戻しつつ、係数を調整して止まりやすくする。

        float damping = 1.0f;

        if (rb.isGrounded) {
            // 接地時: dynamicFrictionを使って強く減速させる
            // friction 0.5 のとき、1秒で速度が約 1/10 になるくらいの強さ
            // dt * 5.0f などを掛けて効きを良くする
            float frictionPower = rb.dynamicFriction * 10.0f;
            damping             = 1.0f / (1.0f + (frictionPower * dt));
        }
        else {
            // 空中: 空気抵抗
            damping = rb.linearDamping;
        }

        rb.velocity = rb.velocity * damping;

        Vector3 rbVelocity = rb.velocity;

        // 微小速度なら完全に止める（ピタッと止まる感覚のため）
        if (rbVelocity.LengthSquared() < 0.01f) {
            rb.velocity = {0, 0, 0};
        }

        // ---------------------------------------------------
        // 2. 移動量の算出と壁判定 (Wall Slide)
        // ---------------------------------------------------
        Vector3 velocity = rb.velocity;
        Vector3 delta    = velocity * dt; // このフレームでの移動予定量

        // マップがある場合のみ壁判定を行う
        if (map && !rb.ignoreMapCollision) {
            // キャラクターの判定半径（仮で0.4m）
            float radius = 0.4f;

            // --- X軸の移動判定 ---
            // 「現在地 + X移動」の場所が壁かどうか
            float   nextX = trans.position.x + delta.x;
            Vector3 testPosX(nextX, trans.position.y, trans.position.z);

            if (CheckWallCollision(*map, testPosX, radius)) {
                // 壁に当たるならX移動をキャンセル
                delta.x    = 0;
                velocity.x = 0; // 速度も殺す（壁に張り付くのを防ぐため反発させても良い）
            }

            // --- Z軸の移動判定 ---
            // 「現在地 + (補正済みX) + Z移動」の場所が壁かどうか
            // ※斜め移動の際、Xですでに止まった場合はその位置基準でZを判定する
            float   nextZ = trans.position.z + delta.z;
            Vector3 testPosZ(trans.position.x + delta.x, trans.position.y, nextZ);

            if (CheckWallCollision(*map, testPosZ, radius)) {
                // 壁に当たるならZ移動をキャンセル
                delta.z    = 0;
                velocity.z = 0;
            }

            // 補正後の速度を書き戻す（壁にぶつかって止まった状態を反映）
            rb.velocity = velocity;
        }


        Vector3 finalVelocity = rb.velocity;
        // -----------------------------------------------------------------------
        // ★追加: 速度の安全装置 (Speed Limit)
        // AIや衝突計算が暴走しても、個体に設定された maxSpeed 以上は絶対に出させない
        // -----------------------------------------------------------------------
        float currentSpeedSq = finalVelocity.LengthSquared();
        float maxSpeedSq     = rb.maxSpeed * rb.maxSpeed;

        // もし最高速度を超えていたらカットする
        if (currentSpeedSq > maxSpeedSq && currentSpeedSq > 0.0001f) {
            float scale = std::sqrt(maxSpeedSq / currentSpeedSq);
            rb.velocity = rb.velocity * scale;
        }

        // ---------------------------------------------------
        // 3. 座標の更新 (Integration)
        // ---------------------------------------------------
        Vector3 finalDelta = rb.velocity * dt; // 修正後の速度で移動量を再計算
        trans.position     = trans.position + finalDelta;
        // ★修正: 地面(Y=0)との衝突判定と接地フラグ更新
        if (trans.position.y <= 0.0f) {
            // 地面にめり込んだので押し戻す
            trans.position.y = 0.0f;

            // 接地フラグをON
            rb.isGrounded = true;

            // 落下速度をリセット（これがないと重力が蓄積し続ける）
            if (rb.velocity.y < 0.0f) {
                rb.velocity.y = 0.0f;
            }
        }
        else {
            // 空中にいる
            rb.isGrounded = false;
        }
    });
}

// 壁判定ヘルパー関数
bool PhysicsIntegrationSystem::CheckWallCollision(
    const StageMap &map, const DirectX::SimpleMath::Vector3 &pos, float r) {
    int gx, gz;

    // 1. 中心判定
    map.WorldToGrid(pos, gx, gz);
    if (map.IsWall(gx, gz))
        return true;

    // 2. 四隅判定（判定半径の9割程度の位置を確認して、角への引っかかりを防ぐ）
    float offset = r * 0.9f;

    // 右 (X+)
    map.WorldToGrid(pos + Vector3(offset, 0, 0), gx, gz);
    if (map.IsWall(gx, gz))
        return true;

    // 左 (X-)
    map.WorldToGrid(pos + Vector3(-offset, 0, 0), gx, gz);
    if (map.IsWall(gx, gz))
        return true;

    // 下/手前 (Z+)
    map.WorldToGrid(pos + Vector3(0, 0, offset), gx, gz);
    if (map.IsWall(gx, gz))
        return true;

    // 上/奥 (Z-)
    map.WorldToGrid(pos + Vector3(0, 0, -offset), gx, gz);
    if (map.IsWall(gx, gz))
        return true;

    return false;
}

// 自動登録
REGISTER_LOGIC_SYSTEM(PhysicsIntegrationSystem, Priority::LogicStage::Physics);