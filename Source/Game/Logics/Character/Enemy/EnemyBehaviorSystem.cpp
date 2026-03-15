#include "EnemyBehaviorSystem.h"
#include <DirectXMath.h>
#include <SimpleMath.h>
#include <imgui.h>

#include "Engine/Graphics/Renderer/ShapeRenderer.h"
#include "Game/Logics/Combat/HealthComponent.h"
#include "Game/Logics/Character/Player/PlayerComponent.h"
#include "Game/Logics/World/StageMap.h"
#include "Engine/Physics/Grid.h"
#include "CombatDirector.h"

#include "Game/Logics/Combat/Weapon/Core/GunBuilder.h"
#include "Game/Logics/Combat/GlobalDamageQueue.h"

#include "Engine/Physics/CollisionHelper.h"

// システムの実行順序の定義ヘッダー
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

// ===================================================================================
// ファイル名: EnemyBehaviorSystem.cpp
// クラス名: EnemyBehaviorSystem
//
// 【実装詳細】
//   ・ハイブリッドAIアーキテクチャ（FSM + Steering Behavior）の実装
//
// 【アルゴリズム詳細】
//   1. Chase（移動）ロジック:
//      - 単純な直線移動ではなく、ベクトルの合成によって障害物を避けながら進む
//      - 壁回避には「3方向レイキャスト（触角）」を使用：
//          - 正面(Forward): 壁があれば強力なブレーキと反発力を生成
//          - 左右(Left/Right): 斜めの壁を検知し、壁沿いに旋回する力を生成
//      - これにより、停止状態からの復帰や、複雑な地形成の走破が可能
//
//   2. 群衆シミュレーション:
//      - Gridシステムを活用し、近傍の敵同士が重ならないように「分離力」を適用
//
//   3. 攻撃ロジック:
//      - 予備動作（軸合わせ） -> 突進・発射 -> クールダウン のシーケンスを実行
// ===================================================================================

using namespace DirectX::SimpleMath;
using namespace CCL::ECS;

void EnemyBehaviorSystem::Update(float dt)
{
    // 必要なリソースの取得
    const Grid     *grid = _world->HasResource<Grid>() ? &_world->GetResource<Grid>() : nullptr;
    StageMap *map  = _world->HasResource<StageMap>() ? &_world->GetResource<StageMap>() : nullptr;

    // ディレクターの取得
    if (!_world->HasResource<CombatDirector>()) {
        _world->AddResource<CombatDirector>();
    }
    auto &director = _world->GetResource<CombatDirector>();

    // ===========================================================================
    // ★トークン数の自動修復 (Auto-Correction)
    // ===========================================================================
    // 生きている敵の中で、実際にトークンを持っている数を数え直す。
    // これにより、敵が死んだ際の「トークン持ち逃げ（リーク）」を完全に防止します。
    int actualUsedCount = 0;

    // EnemyComponentを持つ全エンティティを走査
    // Viewは「今現在生きているエンティティ」だけを返すので、死んだ敵はカウントされません
    for (auto id : _world->View<EnemyComponent>()) {
        auto *e = _world->GetComponent<EnemyComponent>(id);
        if (e && e->hasAttackToken) {
            actualUsedCount++;
        }
    }

    // ディレクターの数値を正しい値で上書き
    director.usedTokens = actualUsedCount;


    // 1. プレイヤー位置の特定
    Vector3  playerPos     = Vector3::Zero;
    Vector3  playerForward = Vector3::UnitZ; // プレイヤーの向いている方向
    bool     isPlayerFound = false;
    EntityID playerID      = InvalidEntityID;

    auto playerView = _world->View<PlayerComponent, TransformComponent>();
    if (!playerView.empty()) {
        playerID     = playerView[0];
        auto *pTrans = _world->GetComponent<TransformComponent>(playerID);
        if (pTrans) {
            // プレイヤー位置もワールド座標で取得
            playerPos     = pTrans->GetWorldPosition();
            playerForward = Vector3::Transform(Vector3::UnitZ, pTrans->rotation);
            isPlayerFound = true;
        }
    }

    // フローフィールドの更新
    // 毎フレーム計算するのは重いので、「プレイヤーが前のマスから移動した時」のみ更新する
    // ★スパイスB: 更新の分散 (ラウンドロビン方式)
    if (isPlayerFound && map) {
        static int lastPx = -9999;
        static int lastPz = -9999;
        
        // フレームカウントを使って、毎フレーム更新するスロットを変える
        // Frame 1: Slot 0更新
        // Frame 2: Slot 1更新 ...
        static int frameCount = 0;
        frameCount++;

        int px, pz;
        map->WorldToGrid(playerPos, px, pz);

        // プレイヤーが大きく動いた、かつ一定間隔ごとに少しずつ更新
        bool isPosChanged = (px != lastPx || pz != lastPz);

        // プレイヤーがグリッドをまたいだら更新
        // 「位置が変わった」または「定期更新(念のため)」
        if (isPosChanged || (frameCount % 10 == 0)) {

            // 包囲するためのオフセット（マス数）
            // 例: 3マス(6m) 離れた位置を取り囲む
            int offset = 3;

            // 今のフレームで担当するスロットだけを更新する
            // これにより、右から来る敵と左から来る敵の反応が微妙にズレる
            int slotToUpdate = frameCount % StageMap::FLOW_FIELD_COUNT;

            switch (slotToUpdate) {
            case 0: map->UpdateFlowField(0, px, pz); break;
            case 1: map->UpdateFlowField(1, px + offset, pz); break;
            case 2: map->UpdateFlowField(2, px - offset, pz); break;
            case 3: map->UpdateFlowField(3, px, pz - offset); break;
            }

            // 位置が変わっていたら座標記録更新
            if (isPosChanged) {
                lastPx = px;
                lastPz = pz;
            }
        }
    }

    // 2. 全敵の更新
    ForEachWithID(
        [&](EntityID id, EnemyComponent &enemy, TransformComponent &trans, RigidBodyComponent &rb) {
           
          // --- カリング処理 (Culling) ---
            float cullDist = 50.0f;
            float distSq   = 0.0f;
            if (isPlayerFound) {
                Vector3 diff = playerPos - trans.GetWorldPosition();
                distSq = diff.LengthSquared();
                if (distSq > cullDist * cullDist) {
                    rb.velocity = Vector3::Zero;
                    return; // 遠すぎる場合は更新スキップ
                }
            }

            // --- タイマー更新 ---
            enemy.stateTimer += dt;
            if (enemy.attackCooldown > 0) enemy.attackCooldown -= dt;
            if (enemy.dodgeCooldown > 0) enemy.dodgeCooldown -= dt;
            if (enemy.chargeCooldown > 0) enemy.chargeCooldown -= dt;
            if (enemy.jumpCooldown > 0) enemy.jumpCooldown -= dt;   // ジャンプクールダウンの更新

            // --- 距離情報の準備 ---
            Vector3 myWorldPos   = trans.GetWorldPosition();
            float   distToPlayer = FLT_MAX;
            Vector3 dirToPlayer  = Vector3::Zero;

            if (isPlayerFound) {
                Vector3 diff = playerPos - myWorldPos;
                distToPlayer = diff.Length();
                if (distToPlayer > 0.001f) dirToPlayer = diff / distToPlayer;
            }

            // --- ステートマシン分岐 ---
            // 巨大なswitch文を関数呼び出しに置き換えてスッキリさせる
            switch (enemy.currentState) {
            case EnemyState::Idle:
                UpdateIdleState(enemy, rb, isPlayerFound, distToPlayer);
                break;

            case EnemyState::Chase:
                UpdateChaseState(id, dt, enemy, trans, rb, playerPos, distToPlayer, dirToPlayer, playerForward, grid, map,director);
                break;

            case EnemyState::Charge: // Tank
                UpdateChargeState(dt, enemy, trans, rb, dirToPlayer,director);
                break;

            case EnemyState::Jump:   // Jumper
                UpdateJumpState(enemy, rb,director);
                break;

            case EnemyState::Dodge:  // Evader
                UpdateDodgeState(enemy);
                break;

            case EnemyState::RangeAttack: // Fire
                UpdateRangeAttackState(dt, enemy, trans, dirToPlayer);
                break;

            case EnemyState::Attack: // Melee
                UpdateAttackState(
                    id, dt, enemy, trans, rb, playerID, playerPos, dirToPlayer, director);
                break;

            case EnemyState::Stun:
                if (enemy.stateTimer > 0.5f) {
                    enemy.currentState = EnemyState::Chase;
                }
                break;
            }
        });
}


// ===================================================================================
// State Update Implementations
// ===================================================================================

void EnemyBehaviorSystem::UpdateIdleState(
    EnemyComponent &enemy, RigidBodyComponent &rb, bool isPlayerFound, float distToPlayer)
{
    rb.velocity.x *= 0.9f; // 摩擦停止
    rb.velocity.z *= 0.9f;

    if (isPlayerFound && distToPlayer < enemy.detectRange) {
        enemy.currentState = EnemyState::Chase;
        enemy.stateTimer   = 0.0f;
    }
}

void EnemyBehaviorSystem::UpdateChaseState(EntityID id,
    float                                           dt,
    EnemyComponent                                 &enemy,
    TransformComponent                             &trans,
    RigidBodyComponent                             &rb,
    const Vector3                                  &playerPos,
    float                                           distToPlayer,
    const Vector3                                  &dirToPlayer,
    const Vector3                                  &playerForward,
    const Grid                                     *grid,
    const StageMap                                 *map,
    CombatDirector                                 &director)
{
    // 諦め判定
    if (distToPlayer > enemy.detectRange * 1.5f) {
        enemy.currentState = EnemyState::Idle;
        return;
    }

        // ★追加: 速度倍率の初期値は 1.0 (通常速度)
    float speedMultiplier = 1.0f;


    // --- 各タイプの特殊行動への遷移判定 ---

    // =========================================================
    // 1. Tank: 突進 (Charge) - [実装済み]
    // =========================================================
    if (enemy.aiType == EnemyAIType::Tank && distToPlayer < enemy.attackRange &&
        enemy.chargeCooldown <= 0.0f) {
        // ディレクターに許可を求める
        if (director.RequestToken()) {
            enemy.hasAttackToken = true; // トークンゲット！

            enemy.currentState = EnemyState::Charge;
            enemy.stateTimer   = 0.0f;
            enemy.hasActioned  = false;
            rb.velocity        = rb.velocity * 0.1f; // 予備動作として減速
            return;
        }
        else {
            // ★トークン却下時（順番待ち）

            // 1. 速度は落とす（じりじり動く）
            speedMultiplier = 0.3f;

            // 2. 「旋回ベクトル」を計算して Steering に加える
            // プレイヤーに向かうベクトル(dirToPlayer) と 上方向(Up) の外積 ＝ 横方向
            Vector3 tangent = dirToPlayer.Cross(Vector3::Up);

            // 敵によって「時計回り」か「反時計回り」かを変える
            // IDやnoiseOffsetを使ってランダムに決める
            if ((int)id % 2 == 0) {
                tangent = -tangent;
            }

            // ステアリング移動計算（ComputeSteering）の結果に、この旋回力をブレンドする
            // 下のほうにある ComputeSteering 呼び出し後に加算するのが手っ取り早いです

            // ※ここでフラグを立てておき、下の計算後に足すのが綺麗です
            enemy.isOrbiting = true; // メンバ変数かローカル変数で管理
        }
    }

    // =========================================================
    // 2. Jumper: 跳躍攻撃 (Jump) - [★追加]
    // =========================================================
    if (enemy.aiType == EnemyAIType::Jumper && rb.isGrounded && distToPlayer < enemy.attackRange &&
        enemy.jumpCooldown <= 0.0f) {

        // ★ここでもトークン申請！
        // 許可が出たときだけジャンプできる
        if (director.RequestToken()) {
            enemy.hasAttackToken = true; // トークン確保

            enemy.currentState = EnemyState::Jump;
            enemy.stateTimer   = 0.0f;

            // ジャンプ処理...
            rb.maxSpeed   = 100.0f;
            rb.velocity   = Vector3(0, enemy.jumpForce, 0) + (dirToPlayer * enemy.moveSpeed * 1.9f);
            rb.isGrounded = false;
            enemy.jumpCooldown = enemy.jumpInterval;
            return;
        }
        else {
            // ★トークン却下時（順番待ち）

            // 1. 速度は落とす（じりじり動く）
            speedMultiplier = 0.3f;

            // 2. 「旋回ベクトル」を計算して Steering に加える
            // プレイヤーに向かうベクトル(dirToPlayer) と 上方向(Up) の外積 ＝ 横方向
            Vector3 tangent = dirToPlayer.Cross(Vector3::Up);

            // 敵によって「時計回り」か「反時計回り」かを変える
            // IDやnoiseOffsetを使ってランダムに決める
            if ((int)id % 2 == 0) {
                tangent = -tangent;
            }

            // ステアリング移動計算（ComputeSteering）の結果に、この旋回力をブレンドする
            // 下のほうにある ComputeSteering 呼び出し後に加算するのが手っ取り早いです

            // ※ここでフラグを立てておき、下の計算後に足すのが綺麗です
            enemy.isOrbiting = true; // メンバ変数かローカル変数で管理
        }
    }



    // =========================================================
    // 3. Pawn / Boid: 近接攻撃 (Attack) - [★追加]
    // =========================================================
    // PawnやBoidが殴りかかれる距離に来た場合
    // (BoidもaiTypeチェックに追加してOK)
    if ((enemy.aiType == EnemyAIType::Pawn || enemy.aiType == EnemyAIType::Boid) &&
        distToPlayer < enemy.attackRange && enemy.attackCooldown <= 0.0f) {

        // ★トークン申請
        if (director.RequestToken()) {
            enemy.hasAttackToken = true;
            enemy.currentState   = EnemyState::Attack; // 攻撃（接近）モードへ
            enemy.stateTimer     = 0.0f;
            enemy.hasActioned    = false;
            // 予備動作の減速は削除。そのままの勢いで突っ込ませる
            return;
        }
        else {
            // ★トークン却下時（順番待ち）

            // 1. 速度は落とす（じりじり動く）
            speedMultiplier = 0.3f;

            // 2. 「旋回ベクトル」を計算して Steering に加える
            // プレイヤーに向かうベクトル(dirToPlayer) と 上方向(Up) の外積 ＝ 横方向
            Vector3 tangent = dirToPlayer.Cross(Vector3::Up);

            // 敵によって「時計回り」か「反時計回り」かを変える
            // IDやnoiseOffsetを使ってランダムに決める
            if ((int)id % 2 == 0) {
                tangent = -tangent;
            }

            // ステアリング移動計算（ComputeSteering）の結果に、この旋回力をブレンドする
            // 下のほうにある ComputeSteering 呼び出し後に加算するのが手っ取り早いです

            // ※ここでフラグを立てておき、下の計算後に足すのが綺麗です
            enemy.isOrbiting = true; // メンバ変数かローカル変数で管理
        }
    }

    // 3. Evader: プレイヤーに見られたら回避 (Dodge)
    if (enemy.aiType == EnemyAIType::Evader && enemy.dodgeCooldown <= 0.0f) {
        float dot = playerForward.Dot(-dirToPlayer);
        if (dot > 0.9f) { // 正面約25度以内
            enemy.currentState  = EnemyState::Dodge;
            enemy.stateTimer    = 0.0f;
            enemy.dodgeCooldown = 2.5f;

            // 横方向へ、通常の倍速で弾き飛ばす (飛距離アップ)
            Vector3 right    = dirToPlayer.Cross(Vector3::Up);
            Vector3 dodgeDir = (rand() % 2 == 0) ? right : -right;

            // 少し斜め前にも進ませると「避けながら詰める」感じが出る
             dodgeDir = dodgeDir + (dirToPlayer);
            dodgeDir.Normalize();

            rb.velocity = dodgeDir * (enemy.moveSpeed * 3.0f);
            return;
        }
    }

    // 4. Fire: 射撃体勢
    if (enemy.aiType == EnemyAIType::Fire && distToPlayer < enemy.rangeAttackDist &&
        distToPlayer > enemy.keepDistance) {

        if (director.RequestToken()) {
            enemy.hasAttackToken = true; // トークンゲット！

            enemy.currentState = EnemyState::RangeAttack;
            enemy.stateTimer   = 0.0f;
            rb.velocity        = Vector3::Zero;
            return;
        }
        else
        {
            // ★トークン却下時（順番待ち）

            // 1. 速度は落とす（じりじり動く）
            speedMultiplier = 0.3f;

            // 2. 「旋回ベクトル」を計算して Steering に加える
            // プレイヤーに向かうベクトル(dirToPlayer) と 上方向(Up) の外積 ＝ 横方向
            Vector3 tangent = dirToPlayer.Cross(Vector3::Up);

            // 敵によって「時計回り」か「反時計回り」かを変える
            // IDやnoiseOffsetを使ってランダムに決める
            if ((int)id % 2 == 0) {
                tangent = -tangent;
            }

            // ステアリング移動計算（ComputeSteering）の結果に、この旋回力をブレンドする
            // 下のほうにある ComputeSteering 呼び出し後に加算するのが手っ取り早いです

            // ※ここでフラグを立てておき、下の計算後に足すのが綺麗です
            enemy.isOrbiting = true; // メンバ変数かローカル変数で管理
        }
    }

    // --- 通常移動 (Steering) ---
    Vector3 steering =
        ComputeSteering(id, trans.GetWorldPosition(), rb.velocity, playerPos, enemy, grid, map);
    float actualSpeed = enemy.moveSpeed * enemy.speedRandomness * speedMultiplier;

    // 旋回力をブレンド
    if (enemy.isOrbiting) {
        // 通常の追尾(Steering)は弱めつつ、横移動(Tangent)を強く足す
        Vector3 tangent = dirToPlayer.Cross(Vector3::Up);
        if ((int)id % 2 == 0) tangent = -tangent;

        // 追尾 0.2 : 旋回 0.8 くらいの割合
        steering = steering * 0.4f + tangent * 0.6f;
        steering.Normalize();
    }

    if (enemy.aiType == EnemyAIType::Evader) {
        // 現在の時間と個体差(noiseOffset)を使ってリズムを作る
        // 例: 2秒周期 (1.2秒止まって、0.8秒動く)
        float time  = (float)ImGui::GetTime() + enemy.noiseOffset;
        float cycle = fmodf(time, 2.0f); // 0.0 ~ 2.0 の繰り返し

        if (cycle < 1.2f) {
            // 「様子見」フェーズ: 完全に止まる
            steering    = Vector3::Zero;
            actualSpeed = 0.0f;

            // 止まっている間は、ゆっくりプレイヤーの方を向く（不気味さ演出）
            if (dirToPlayer.LengthSquared() > 0.01f) {
                float yaw      = atan2f(dirToPlayer.x, dirToPlayer.z);

                trans.rotation = Quaternion::Slerp(trans.rotation,
                    Quaternion::CreateFromAxisAngle(Vector3::UnitY, yaw),
                    dt * 2.0f);
            }
        }
        else {
            // 「接近」フェーズ: ゆっくり忍び寄る (速度半分)
            //actualSpeed *= 0.5f;
        }
    }

    if (steering.LengthSquared() > 0.001f) {
        steering.Normalize();
        Vector3 desiredVel = steering * actualSpeed;

        // 慣性の調整 (TankとFireは重く/ふわふわに)
        float turnSpeed = 8.0f;
        if (enemy.aiType == EnemyAIType::Tank) turnSpeed = 2.0f;
        if (enemy.aiType == EnemyAIType::Fire) turnSpeed = 2.0f;

        Vector3 currentVel = rb.velocity;
        float   currentY   = currentVel.y;
        currentVel.y       = 0;

        Vector3 newVel = Vector3::Lerp(currentVel, desiredVel, dt * turnSpeed);
        newVel.y       = currentY;
        rb.velocity    = newVel;

        // 向きの更新
        if (newVel.LengthSquared() > 0.1f) {
            Vector3 faceDir = newVel;
            faceDir.y       = 0;
            faceDir.Normalize();
            float      yaw       = atan2f(faceDir.x, faceDir.z);
            Quaternion targetRot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, yaw);
            trans.rotation       = Quaternion::Slerp(trans.rotation, targetRot, dt * 10.0f);
        }
    }
    // 速度がほぼゼロ(待機)の場合の減速処理
    else if (speedMultiplier < 0.1f) {
        // スピード倍率が極端に低い、またはSteeringがゼロなら摩擦で止める
        Vector3 vel = rb.velocity;
        rb.velocity = vel * 0.9f;
    }
}

void EnemyBehaviorSystem::UpdateChargeState(float dt,
    EnemyComponent                               &enemy,
    TransformComponent                           &trans,
    RigidBodyComponent                           &rb,
    const Vector3                                &dirToPlayer,
    CombatDirector                               &director)
{
    float t1 = enemy.chargeWindUpTime;        // タメ (0.8s)
    float t2 = t1 + enemy.chargeDuration;     // 突進終了 (1.8s)
    float t3 = t2 + enemy.chargeRecoveryTime; // 硬直終了 (3.3s)

    // フェーズ1: タメ（震える）
    if (enemy.stateTimer < t1) {
        // 向き合わせ
        if (dirToPlayer.LengthSquared() > 0.01f) {
            float yaw      = atan2f(dirToPlayer.x, dirToPlayer.z);
            trans.rotation = Quaternion::Slerp(
                trans.rotation, Quaternion::CreateFromAxisAngle(Vector3::UnitY, yaw), dt * 5.0f);
        }

        // 振動演出
        Vector3 forward = Vector3::Transform(Vector3::UnitZ, trans.rotation);
        Vector3 right   = forward.Cross(Vector3::UnitY);
        float   shake   = sinf(enemy.stateTimer * 50.0f) * 0.1f;
        rb.velocity     = right * (shake * 60.0f);
    }
    // フェーズ2: 突進
    else if (enemy.stateTimer < t2) {
        if (!enemy.hasActioned) {
            enemy.hasActioned = true; // 突進開始（SE等あればここ）
        }
        Vector3 forward = Vector3::Transform(Vector3::UnitZ, trans.rotation);
        rb.velocity     = forward * enemy.chargeSpeed;
    }
    // フェーズ3: 硬直
    else if (enemy.stateTimer < t3) {
        rb.velocity = Vector3::Lerp(rb.velocity, Vector3::Zero, dt * 10.0f);
    }
    // 終了
    else {
        enemy.currentState   = EnemyState::Chase;
        enemy.stateTimer     = 0.0f;
        enemy.hasActioned    = false;
        enemy.chargeCooldown = enemy.chargeInterval; // クールダウン開始

        // 借りていたトークンを返す
        if (enemy.hasAttackToken) {
            director.ReturnToken();
            enemy.hasAttackToken = false; // 手放す
        }
    }
}

void EnemyBehaviorSystem::UpdateJumpState(
    EnemyComponent &enemy, RigidBodyComponent &rb, CombatDirector &director)
{
    // 接地したらChaseに戻る（0.2秒は最低保証）
    if (enemy.stateTimer > 0.2f && rb.isGrounded) {
        enemy.currentState = EnemyState::Chase;
        rb.velocity        = Vector3::Zero;

        // ★返却
        if (enemy.hasAttackToken) {
            director.ReturnToken();
            enemy.hasAttackToken = false;
        }
    }
}

void EnemyBehaviorSystem::UpdateDodgeState(EnemyComponent &enemy)
{
    // 0.3秒間ダッシュし続けるだけ
    if (enemy.stateTimer > 0.3f) {
        enemy.currentState = EnemyState::Chase;
    }
}

void EnemyBehaviorSystem::UpdateRangeAttackState(
    float dt, EnemyComponent &enemy, TransformComponent &trans, const Vector3 &dirToPlayer)
{
    // 向き合わせ
    if (dirToPlayer.LengthSquared() > 0.01f) {
        float yaw      = atan2f(dirToPlayer.x, dirToPlayer.z);
        trans.rotation = Quaternion::Slerp(
            trans.rotation, Quaternion::CreateFromAxisAngle(Vector3::UnitY, yaw), dt * 10.0f);
    }

    // 1.0秒溜めて発射
    if (enemy.stateTimer > 1.0f && !enemy.hasActioned) {
        SpawnEnemyBullet(trans.GetWorldPosition(), dirToPlayer);
        enemy.hasActioned = true;
    }

    // 1.5秒で行動終了
    if (enemy.stateTimer > 1.5f) {
        enemy.currentState = EnemyState::Chase;
        enemy.hasActioned  = false;
    }
}

void EnemyBehaviorSystem::UpdateAttackState(EntityID id,
    float                                            dt,
    EnemyComponent                                  &enemy,
    TransformComponent                              &trans,
    RigidBodyComponent                              &rb,
    CCL::ECS::EntityID                               playerID,
    const Vector3                                   &playerPos,
    const Vector3                                   &dirToPlayer,
    CombatDirector                                  &director)
{
    // 1. 移動ロジック：ひたすらプレイヤーに向かう（殺意MAX）
    // ---------------------------------------------------------
    Vector3 moveDir = dirToPlayer;

    // 距離が近すぎる(0.1m以下)と方向が荒ぶるのでチェック
    if (moveDir.LengthSquared() < 0.001f) {
        moveDir = Vector3::Transform(Vector3::UnitZ, trans.rotation); // 前方へ
    }

    // 向きを素早く合わせる
    float yaw      = atan2f(moveDir.x, moveDir.z);
    trans.rotation = Quaternion::Slerp(
        trans.rotation, Quaternion::CreateFromAxisAngle(Vector3::UnitY, yaw), dt * 15.0f);

    // 普段より速く走る（スプリント）
    float sprintSpeed = enemy.moveSpeed * 1.0f;
    rb.velocity       = moveDir * sprintSpeed;

    // 2. 終了判定：接触 または タイムアウト
    // ---------------------------------------------------------
    bool isHit = false;

   // 形状（SphereやBoxなど）を気にせず、ラッパーを使って一括で判定！
    ColliderWrapper myCol = CollisionHelper::GetCollider(_world, id);

    if (myCol.type != ShapeType::None) {
        // isColliding と lastHitEntity はポインタなので、nullptrチェックをしてから中身を見る
        if (myCol.isColliding && *myCol.isColliding) {
            if (myCol.lastHitEntity && *myCol.lastHitEntity == playerID) {
                isHit = true;
            }
        }
    }

   // B. タイムアウト判定の変更
    // ---------------------------------------------------------
    // 条件: 「2秒以上経過」かつ「攻撃トークンが満員」の場合のみ諦める
    // つまり、トークンに空きがあるなら(used < max)、何秒でも追いかけ続ける

    bool isTokenFull = (director.usedTokens >= director.maxAttackTokens);

    // 「時間が過ぎている」AND「席がない」時だけタイムアウト
    bool isTimeout = (enemy.stateTimer > 2.0f) && isTokenFull;

    if (isHit || isTimeout) {

        // 攻撃完了処理
        if (isHit) {
            // 郵便局にプレイヤー宛てのダメージ手紙を投函する！
            if (_world->HasResource<GlobalDamageQueue>()) {
                auto &damageQueue = _world->GetResource<GlobalDamageQueue>();

                // ※ enemyコンポーネントに baseDamage のような変数があればそれに置き換えてください
                float enemyDamage = 10.0f;

                damageQueue.events.push_back({
                    playerID,      // 宛先 (プレイヤー)
                    enemyDamage,   // ダメージ量
                    Vector3::Zero, // ノックバック(必要なら計算する)
                    id,             // 差出人 (この敵自身)
                    "EnemyMeleeAttack" // ダメージの種類 (任意の文字列)
                });
            }

            // ヒットした反動で少しノックバック（後ろへ跳ねる）
            rb.velocity = -moveDir * 5.0f;
        }

        // ステート復帰
        enemy.currentState   = EnemyState::Chase;
        enemy.stateTimer     = 0.0f;
        enemy.attackCooldown = enemy.attackInterval; // 次の攻撃までのクールダウン

        // ★重要: トークンを返却（次の人が攻撃できるようにする）
        if (enemy.hasAttackToken) {
            director.ReturnToken();
            enemy.hasAttackToken = false;
        }
    }
}

// ---------------------------------------------------------
// 賢い移動ベクトルを算出する関数
// ---------------------------------------------------------
Vector3 EnemyBehaviorSystem::ComputeSteering(EntityID id,
    const Vector3                                    &position,
    const Vector3                                    &velocity,
    const Vector3                                    &targetPos,
    const EnemyComponent                             &enemy,
    const Grid                                       *grid,
    const StageMap                                   *map)
{
    Vector3 force = Vector3::Zero;

    // 1. 【目的】Seek (追跡) - フローフィールド対応版
    // ------------------------------------------------------------------
    Vector3 seekDir        = Vector3::Zero;
    bool    usingFlowField = false;

    if (map) {
        // 足元のグリッド座標を取得
        int gx, gz;
        map->WorldToGrid(position, gx, gz);
        int idx = map->GetIndex(gx, gz);

        // ★自分の担当スロットを使用する
        int slot = enemy.formationSlot;

        // Fireタイプは陣形(回り込み)を無視して、常に最短ルート(0)を通る
        // これにより「遠くで外周を回り続ける」現象を防ぐ
        if (enemy.aiType == EnemyAIType::Fire) {
            slot = 0;
        }
        else {
            // 2. その他の敵(Pawn, Tank等)の「攻撃スイッチ」
            // プレイヤーとの直線距離を計算
            float distToPlayer = Vector3::Distance(position, targetPos);

            // ★修正後: 担当スロットが「正面(0)」以外なら、もっと近づくまで迂回を維持させる
            // 攻撃射程(attackRange)の少し手前まで来たら、初めてプレイヤーに直行する

            // 「包囲解除」距離を計算（attackRange の 1.2倍くらい）
            float engageDist = enemy.attackRange * 0.2f;

            // 安全マージン（近すぎたらさすがにプレイヤーを見る）
            if (engageDist < 4.0f) engageDist = 4.0f;

            // 距離がこれより近づいたら、観念して直線ルート(0)に切り替える
            if (distToPlayer < engageDist) {
                slot = 0;
            }
        }

        // 安全策: もしスロットが範囲外なら0にする
        if (slot < 0 || slot >= StageMap::FLOW_FIELD_COUNT) slot = 0;

        // 指定スロットのベクトルを取得
        // map->flowFields[slot] にアクセス
        const auto &targetField = map->flowFields[slot];


        // ベクトル場から「補間された方向」を取得
        // idx を使って配列を直接見るのをやめる
        Vector3 flow = map->GetFlowDirection(slot, position);

        // ★フォールバック処理
        // もし「右回りルート(Slot1)」が壁の中で生成失敗していたり、
        // 孤立エリアでベクトルが無い場合は、メインルート(Slot0)を借りる
        if (flow.LengthSquared() < 0.01f && slot != 0) {
            // メインルート(Slot0)で再取得
            flow = map->GetFlowDirection(0, position);
        }

        // ベクトルが有効（0,0,0ではない）なら採用
        if (flow.LengthSquared() > 0.01f) {
            seekDir        = flow;
            usingFlowField = true;
        }
        

        // ★追加: スムージング（視線ショートカット）
        // 「フローフィールドの通りに進もうとしているが、目的地が直接見えるなら近道する」
        if (usingFlowField) {
            // 1. 自分の本来の目的地（包囲スロットの位置）を計算
            //    ※Update関数内のロジック(offset=3)と合わせる
            int px, pz;
            map->WorldToGrid(targetPos, px, pz);
            int offset = 3;

            int tx = px, tz = pz;
            switch (slot) {
            case 0: break;               // Center
            case 1: tx += offset; break; // Right
            case 2: tx -= offset; break; // Left
            case 3: tz -= offset; break; // Back
            }

            // 2. 目的地が壁でなければショートカット判定
            if (!map->IsWall(tx, tz)) {
                Vector3 destPos = map->GridToWorld(tx, tz);
                Vector3 toDest  = destPos - position;
                float   dist    = toDest.Length();

                // 距離がある程度ある場合のみチェック（近すぎると計算不要）
                if (dist > 0.5f) {
                    Vector3 dirToDest = toDest / dist;

                    // ★レイキャスト: 目的地まで壁がないか確認
                    // StageMap::Raycast は「壁に当たった情報」を返す
                    auto hit = map->Raycast(position, dirToDest, dist);

                    if (!hit.isHit) {
                        // 遮蔽物なし！フローフィールドを無視して直行ルートを採用
                        seekDir = dirToDest;
                    }
                }
            }
        }

        if (usingFlowField) {
            // ★スパイスC: 動きへのノイズ混入
            // sin/cos を使って、進行方向に対して垂直に少し揺らす
            float time  = (float)ImGui::GetTime();               // ゲーム内時間
            float noise = sinf(time * 2.0f + enemy.noiseOffset); // 敵ごとに周期をズレさせる

            // ★追加 : レーン補正(Lane Bias)
            // speedRandomness (0.8~1.2) を利用して、
            // 1.0より速いなら右へ、遅いなら左へ、最大15度くらい傾ける
            float speedFactor = enemy.speedRandomness - 1.0f; // -0.2 ~ +0.2

            // バイアス角度 (-0.3ラジアン ~ +0.3ラジアン ≒ ±17度)
            // 速い敵ほど外側に膨らんで追い抜こうとする動きになる
            float laneBias = speedFactor * 1.5f;

            // Y軸回転で少しベクトルをずらす
            auto rot = Matrix::CreateRotationY(noise * 0.2f); // 最大0.2ラジアン(約11度)のブレ
            seekDir  = Vector3::Transform(seekDir, rot);
        }
    }

    // フローフィールドが使えなかった場合（マップ外や計算前）は、従来の直線Seek
    Vector3 toTarget = targetPos - position;
    if (!usingFlowField) {
        if (toTarget.LengthSquared() > 0.001f) {
            toTarget.Normalize();
            seekDir = toTarget;
        }
    }

    // タイプ別Seek補正
    if (enemy.aiType == EnemyAIType::Fire) {
        // 現在のプレイヤーとの直線距離を測る
        Vector3 toTargetDirect = targetPos - position;
        float   dist           = toTargetDirect.Length();

        if (dist < enemy.keepDistance) {
            // 近すぎる場合: FlowField(seekDir)は無視して、直接プレイヤーから離れるベクトルを使う
            // (FlowFieldは「近づくルート」を示しているので、逆走すると壁に向かう可能性があるが
            //  緊急回避としては単純な逆ベクトルの方が確実)
            if (dist > 0.001f)
                toTargetDirect.Normalize();
            else
                toTargetDirect = Vector3::UnitZ; // 重なっていたらZ方向へ

            // 逃げる力 (強めに)
            force -= toTargetDirect * enemy.weightSeek * 10.0f;
        }
        else {
            // 距離が十分あるなら、通常のルートで近づく
            force += seekDir * enemy.weightSeek;
        }
    }
    else {
        // その他の敵
        force += seekDir * enemy.weightSeek;
    }


    // 2. 【分離】仲間と重ならない力 (Separation)
    // ------------------------------------------------------------------
    if (grid) {
        Vector3 separation = ComputeSeparation(id, position, *grid);
        force += separation * enemy.weightSeparation;
    }

    // 3. 【回避】壁を避ける力 (Wall Avoidance) - "Airbag"
    // ------------------------------------------------------------------
    // フローフィールドがあるため、基本的には壁に突っ込まないルートを通るが、
    // 分離力で壁に押し付けられた時などのための「保険（エアバッグ）」として機能させる
    if (map) {
        Vector3 checkDir = velocity;
        if (checkDir.LengthSquared() < 0.1f) {
            // 停止中は「行きたい方向」をチェック
            checkDir = seekDir;
        }

        Vector3 avoidance = ComputeWallAvoidance(position, checkDir, *map);
        force += avoidance * enemy.weightWall;
    }

    return force;
}


Vector3 EnemyBehaviorSystem::ComputeWallAvoidance(
    const Vector3 &position, const Vector3 &velocity, const StageMap &map)
{
    Vector3 force = Vector3::Zero;

    // 速度がほぼゼロなら、回避計算の基準ベクトル(velocity)も小さい
    // 完全に止まっていると計算不能になるので、最低限の長さを確保
    Vector3 currentDir = velocity;
    float   speed      = currentDir.Length();
    if (speed < 0.1f) {
        // 止まっている時は「前方」と仮定するが、触角は短くする
        speed = 1.0f;
        currentDir = Vector3::UnitZ;
    }
    else {
        currentDir /= speed; // Normalize
    }

    // 触角の設定
    // 
    // 触角の長さを「速度」と「狭さ」に合わせて動的に変える
    // 狭い場所(1マス=2.0m)で4.0mも見ると過剰反応する。
    // 最低1.0m, 最大3.0m程度に抑える。
    float dynamicLookAhead = std::clamp(speed * 0.5f, 1.5f, 3.0f);// 正面の視界距離
   
    float sideFeelerScale = 0.7f;                               // 横の視界は少し短く (2.8m)
    float angle           = DirectX::XMConvertToRadians(30.0f); // 45度左右を見る

    // 回転行列の作成
    auto rotLeft  = DirectX::SimpleMath::Matrix::CreateRotationY(-angle);
    auto rotRight = DirectX::SimpleMath::Matrix::CreateRotationY(angle);

    Vector3 leftDir  = Vector3::Transform(currentDir, rotLeft);
    Vector3 rightDir = Vector3::Transform(currentDir, rotRight);

    // --- 1. 正面レイキャスト ---
    auto hitCenter = map.Raycast(position, currentDir, dynamicLookAhead);

    // --- 2. 左レイキャスト ---
    auto hitLeft = map.Raycast(position, leftDir, dynamicLookAhead * sideFeelerScale);

    // --- 3. 右レイキャスト ---
    auto hitRight = map.Raycast(position, rightDir, dynamicLookAhead * sideFeelerScale);

    // 単純な加算ではなく「最も危険な壁」に対処する
    // 複数ヒットした場合、平均化すると力が打ち消し合うことがある。

    // ヒットした中で「最も近く」「最も正面に近い」壁からの回避を優先するロジック
    // ここでは簡易的に「各ヒットごとの回避力」を積算するが、係数を下げる。

    const float AVOID_STRENGTH = 1.5f; // 5.0fから大幅に下げる

    if (hitCenter.isHit) {
        // 壁からの反発 + 壁にめり込まないための押し出し
        // 法線方向への力を加える
        force += hitCenter.normal * AVOID_STRENGTH * 2.0f; // 正面は緊急度高
    }

    if (hitLeft.isHit) {
        // 左に壁 -> 右に避けるのではなく「壁の法線方向に押し返す」のが物理的に安定する
        force += hitLeft.normal * AVOID_STRENGTH;
    }

    if (hitRight.isHit) {
        force += hitRight.normal * AVOID_STRENGTH;
    }

    // Y軸（上下）の成分は完全に消す
    // 坂道などで法線が斜め上を向いていると、空へ飛ぼうとしてしまうため
    force.y = 0.0f;

    // 力が大きくなりすぎないようクリップする (最大でも移動速度の2倍程度まで)
    if (force.LengthSquared() > 0.001f) {
        float maxForce = 10.0f;
        if (force.LengthSquared() > maxForce * maxForce) {
            force.Normalize();
            force *= maxForce;
        }
    }

    return force;
}

// 分離（仲間と重ならない）
Vector3 EnemyBehaviorSystem::ComputeSeparation(
    EntityID selfID, const Vector3 &position, const Grid &grid)
{
    Vector3 force  = Vector3::Zero;
    int     count  = 0;
    float   radius = 1.0f; // 感知半径

    // positionの座標にあるセルにいるオブジェクトを調べる
    grid.ForEachInCell(position, [&](EntityID otherID, ColliderType type) {
        if (selfID == otherID) return;

        // ★重要: ここは用途に合わせて調整してください
        // プレイヤーと敵、両方から離れたい場合は以下のようにチェックを広げます
        bool isPlayer = _world->HasComponent<PlayerComponent>(otherID);
        bool isEnemy  = _world->HasComponent<EnemyComponent>(otherID);

        // プレイヤーでも敵でもなければ無視（壁などは無視）
        if (!isPlayer && !isEnemy) return;

        auto *t = _world->GetComponent<TransformComponent>(otherID);
        if (!t) return;

        // 自分と相手との距離を計算
        Vector3 diff   = position - t->position;
        float   distSq = diff.LengthSquared();

        // 判定半径内なら計算
        if (distSq < radius * radius) {
            float dist = sqrtf(distSq);

            // ゼロ除算・完全重なり対策
            if (dist < 0.0001f) {
                diff = Vector3(1.0f, 0.0f, 0.0f);
                dist = 0.0001f;
            }

            // 方向ベクトル（正規化）
            Vector3 dir = diff / dist;

            // 物理演算を切ったので、減速なしでフルパワー(1.0f)で押し返してOKです。
            float maxStrength = 1.0f;

            float strength = (1.0f - (dist / radius)) * maxStrength;

            force += dir * strength;

            count++;
        }
    });

    if (count > 0) {
        force /= (float)count;
        // Normalizeはしない（弱い力は弱いままにするため）
    }
    return force;
}

void EnemyBehaviorSystem::SpawnEnemyBullet(const Vector3 &position, const Vector3 &direction) 
{

    // GunBuilder（弾の設計図）を一時的に作成
    GunBuilder gun;

    // 敵用の弾パラメータ設定
    gun.baseSpeed       = 12.0f; // プレイヤーの弾より少し遅めが見切りやすい
    gun.baseBulletCount = 1;     // 1発
    gun.spawnOffset     = 1.5f;  // 体の少し前から出す

    // エフェクトを乗せるならここで設定
    // gun.onHitMods.push_back(EffectID::Knockback);

    // 発射！ (Worldへのポインタ、位置、方向を渡す)
    gun.EnemyFire(_world, position, direction);

}

// ---------------------------------------------------------
// デバッグGUI表示 (OnGui)
// ---------------------------------------------------------
// GameLogics/Enemy/EnemyBehaviorSystem.cpp

// ---------------------------------------------------------
// デバッグGUI表示 (OnGui)
// ---------------------------------------------------------
void EnemyBehaviorSystem::OnGui()
{
    // ディレクター情報の取得
    if (_world->HasResource<CombatDirector>()) {
        auto &director = _world->GetResource<CombatDirector>();

        // --- Combat Director Status ---
        if (ImGui::CollapsingHeader("Combat Director Info", ImGuiTreeNodeFlags_DefaultOpen)) {

            // トークン使用率のプログレスバー表示
            float fraction = (float)director.usedTokens / (float)director.maxAttackTokens;
            char  buf[32];
            sprintf_s(buf, "%d / %d", director.usedTokens, director.maxAttackTokens);

            // 満員なら赤、余裕があれば緑
            if (fraction >= 1.0f)
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
            else
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));

            ImGui::ProgressBar(fraction, ImVec2(-1, 0), buf);
            ImGui::PopStyleColor();

            ImGui::Text("Active Tokens: %d", director.usedTokens);

            // 強制リセットボタン（バグって埋まった時用）
            if (ImGui::Button("Reset Tokens")) {
                director.Reset();
            }
        }
    }

    ImGui::Separator();

    // --- 既存の敵リスト表示 ---
    ImGui::Columns(6, "EnemyColumns"); // カラム数を6に増やす
    ImGui::Separator();
    ImGui::Text("ID");
    ImGui::NextColumn();
    ImGui::Text("Type");
    ImGui::NextColumn();
    ImGui::Text("State");
    ImGui::NextColumn();
    ImGui::Text("Token");
    ImGui::NextColumn(); // ★追加
    ImGui::Text("Dist");
    ImGui::NextColumn();
    ImGui::Text("Timer");
    ImGui::NextColumn();
    ImGui::Separator();

    // プレイヤー情報の取得（距離計算用）
    Vector3 playerPos     = Vector3::Zero;
    bool    isPlayerFound = false;
    auto    playerView    = _world->View<PlayerComponent, TransformComponent>();
    if (!playerView.empty()) {
        auto *pTrans = _world->GetComponent<TransformComponent>(playerView[0]);
        if (pTrans) {
            playerPos     = pTrans->GetWorldPosition();
            isPlayerFound = true;
        }
    }

    // 全敵リスト表示
    ForEachWithID(
        [&](EntityID id, EnemyComponent &enemy, TransformComponent &trans, RigidBodyComponent &rb) {
            // 1. ID
            ImGui::Text("%llu", id);
            ImGui::NextColumn();

            // 2. Type
            switch (enemy.aiType) {
            case EnemyAIType::Pawn: ImGui::Text("Pawn"); break;
            case EnemyAIType::Tank:
                ImGui::TextColored(ImVec4(0.4f, 0.4f, 1.0f, 1.0f), "Tank");
                break;
            case EnemyAIType::Jumper:
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Jumper");
                break;
            case EnemyAIType::Evader:
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 1.0f, 1.0f), "Evader");
                break;
            case EnemyAIType::Boid:
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Boid");
                break;
            case EnemyAIType::Fire:
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Fire");
                break;
            default: ImGui::Text("Unknown");
            }
            ImGui::NextColumn();

            // 3. State
            switch (enemy.currentState) {
            case EnemyState::Idle: ImGui::TextDisabled("Idle"); break;
            case EnemyState::Chase: ImGui::Text("Chase"); break;
            case EnemyState::Attack:
                ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "Attack");
                break;
            case EnemyState::Stun:
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Stun");
                break;
            case EnemyState::Charge:
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "CHARGE!");
                break;
            case EnemyState::Jump:
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Jump");
                break;
            case EnemyState::Dodge:
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f), "Dodge");
                break;
            case EnemyState::RangeAttack:
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Shoot");
                break;
            }
            ImGui::NextColumn();

            // 4. Token (★追加)
            if (enemy.hasAttackToken) {
                ImGui::TextColored(ImVec4(1.0f, 0.2f, 1.0f, 1.0f), "[ O ]"); // 目立つ色で表示
            }
            else {
                ImGui::TextDisabled("[ . ]");
            }
            ImGui::NextColumn();

            // 5. Dist
            if (isPlayerFound) {
                float dist = Vector3::Distance(trans.GetWorldPosition(), playerPos);
                if (dist < enemy.attackRange)
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "%.1fm", dist);
                else if (dist < enemy.detectRange)
                    ImGui::TextColored(ImVec4(1, 1, 0, 1), "%.1fm", dist);
                else
                    ImGui::Text("%.1fm", dist);
            }
            else {
                ImGui::Text("-");
            }
            ImGui::NextColumn();

            // 6. Timer
            if (enemy.attackCooldown > 0)
                ImGui::TextColored(
                    ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "CD: %.1f", enemy.attackCooldown);
            else if (enemy.stateTimer > 0)
                ImGui::Text("T: %.1f", enemy.stateTimer);
            else
                ImGui::Text("-");
            ImGui::NextColumn();
        });

    ImGui::Columns(1);
}

// ---------------------------------------------------------
// 空間デバッグ描画 (OnDrawDebug)
// ---------------------------------------------------------
void EnemyBehaviorSystem::OnDrawDebug(ShapeRenderer *renderer)
{
    // ... (リソース取得やプレイヤー取得は既存のまま) ...
    // リソース取得
    const Grid     *grid = _world->HasResource<Grid>() ? &_world->GetResource<Grid>() : nullptr;
    const StageMap *map =
        _world->HasResource<StageMap>() ? &_world->GetResource<StageMap>() : nullptr;

    // プレイヤー位置取得
    Vector3 playerPos     = Vector3::Zero;
    bool    isPlayerFound = false;
    auto    playerView    = _world->View<PlayerComponent, TransformComponent>();
    if (!playerView.empty()) {
        auto *pTrans = _world->GetComponent<TransformComponent>(playerView[0]);
        if (pTrans) {
            playerPos     = pTrans->GetWorldPosition();
            isPlayerFound = true;
        }
    }

    ForEachWithID([&](CCL::ECS::EntityID        id,
                      EnemyComponent     &enemy,
                      TransformComponent &trans,
                      RigidBodyComponent &rb) {
        Vector3 myWorldPos = trans.GetWorldPosition();
        Vector3 start      = myWorldPos + Vector3(0, 0.5f, 0);

        // --- 1. 範囲の可視化 (既存のコード) ---
        // ... (索敵円などの描画はそのまま) ...
        // 索敵範囲 (薄い黄色)
        renderer->DrawWireframeCylinder(
            myWorldPos, enemy.detectRange, 1.0f, Vector4(1.0f, 1.0f, 0.2f, 1.0f));
        // 攻撃範囲 (赤色)
        renderer->DrawWireframeCylinder(
            myWorldPos, enemy.attackRange, 1.0f, Vector4(1.0f, 0.2f, 0.2f, 1.0f));

        // --- 2. トークン保持者の強調表示 (★新規追加) ---
        if (enemy.hasAttackToken) {
            // 頭上に目立つ「◆」マークを出す
            Vector3 tokenMarkPos = myWorldPos + Vector3(0, 2.5f, 0); // かなり高い位置

            // 紫色のひし形（ボックスを45度回転させるとひし形っぽくなりますが、ここでは単純にBox）
            // 実際はWireframeBoxでもSphereでもOK
            // 回転角度（ラジアン）を計算
            float rotateY = (float)ImGui::GetTime() * 2.0f;

            // 第2引数には Vector3(x, y, z) で角度を渡す
            renderer->DrawBox(tokenMarkPos,
                Vector3(0.0f, rotateY, 0.0f),   // Y軸のみ回転させる
                Vector3(0.3f, 0.3f, 0.3f),      // サイズ
                Vector4(1.0f, 0.0f, 1.0f, 1.0f) // 色
            );

            // 敵とマークを繋ぐ線
            renderer->DrawLine(start, tokenMarkPos, Vector4(1.0f, 0.0f, 1.0f, 0.5f));
        }

        // --- 3. ステート可視化 (State Marker) ---
        // ... (既存のコード) ...
        Vector4 stateColor = Vector4(1, 1, 1, 1);
        switch (enemy.currentState) {
        case EnemyState::Idle: stateColor = Vector4(0.5f, 0.5f, 0.5f, 1.0f); break;
        case EnemyState::Chase: stateColor = Vector4(1.0f, 0.8f, 0.0f, 1.0f); break;
        case EnemyState::Attack: stateColor = Vector4(1.0f, 0.0f, 0.0f, 1.0f); break;
        case EnemyState::Stun: stateColor = Vector4(1.0f, 1.0f, 0.0f, 1.0f); break;
        case EnemyState::Charge: stateColor = Vector4(0.8f, 0.0f, 0.2f, 1.0f); break;
        case EnemyState::Jump: stateColor = Vector4(0.0f, 1.0f, 1.0f, 1.0f); break;
        case EnemyState::Dodge: stateColor = Vector4(1.0f, 0.0f, 1.0f, 1.0f); break;
        case EnemyState::RangeAttack: stateColor = Vector4(1.0f, 0.5f, 0.0f, 1.0f); break;
        }

        Vector3 markerPos = start + Vector3(0, 1.2f, 0);
        renderer->DrawBox(markerPos, Vector3::Zero, Vector3(0.2f, 0.2f, 0.2f), stateColor);
        renderer->DrawLine(start, start + rb.velocity, Vector4(1.0f, 1.0f, 1.0f, 0.8f));
    });
}


REGISTER_LOGIC_SYSTEM(EnemyBehaviorSystem, Priority::LogicStage::Update);