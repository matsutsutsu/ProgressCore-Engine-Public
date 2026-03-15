#include "BossBehaviorSystem.h"
#include "Engine/Serialization/Factory/Prefab.h"
#include "Game/Logics/Combat/HealthComponent.h"
#include "Game/Logics/Character/Player/PlayerComponent.h"
#include "Engine/Graphics/Renderer/ShapeRenderer.h"
#include "Game/Logics/Combat/HealthComponent.h"
#include "Engine/Graphics/Resource/Model.h"
#include <DirectXMath.h>

// システムの実行順序の定義ヘッダー
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

using namespace DirectX::SimpleMath;
using namespace CCL::ECS;

void BossBehaviorSystem::Update(float dt)
{
    // 1. プレイヤー位置取得
    Vector3 playerPos     = Vector3::Zero;
    bool    isPlayerFound = false;
    auto    playerView    = _world->View<PlayerComponent, TransformComponent>();
    if (!playerView.empty()) {
        playerPos     = _world->GetComponent<TransformComponent>(playerView[0])->GetWorldPosition();
        isPlayerFound = true;
    }

    // 2. ボスの更新ループ
    ForEachWithID([&](EntityID            id,
                      BossComponent      &boss,
                      TransformComponent &trans,
                      RigidBodyComponent &rb,
                      AnimatorComponent  &anim,
                      const ModelComponent     &model) {

        // --- アニメーションIDの取得 (初回のみ) ---
        if (model.modelPtr) {
            if (_animIdMove == -1) _animIdMove = model.modelPtr->GetAnimationIndex("Move");
            if (_animIdSlash == -1) _animIdSlash = model.modelPtr->GetAnimationIndex("Slash");
            if (_animIdTackle == -1) _animIdTackle = model.modelPtr->GetAnimationIndex("Tackle");
            if (_animIdSummon == -1)  _animIdSummon = model.modelPtr->GetAnimationIndex("Samon");
            
        }

        // ★重要: Luaのために距離を計算して格納しておく
        if (isPlayerFound) {
            boss.distanceToPlayer = Vector3::Distance(trans.GetWorldPosition(), playerPos);
        }
        else {
            boss.distanceToPlayer = 9999.0f;
        }


        boss.stateTimer += dt;

        // クールダウンの更新 (Luaで使うため)
        if (boss.actionCooldown > 0) boss.actionCooldown -= dt;
        if (boss.summonCooldown > 0) boss.summonCooldown -= dt;
        //if (boss.shockwaveCooldown > 0) boss.shockwaveCooldown -= dt;


        // --- ステートマシン ---
        switch (boss.state) {
        case BossState::Idle:
            // 何もしない、次の行動を決めるのはLuaScriptSystemの仕事

            // 待機中もMoveモーションで威圧する（または専用のIdleがあればそちら）
            anim.Play(_animIdMove, true, 1.0f, 0.2f);
            break;

        case BossState::Move: UpdateMove(dt, boss, trans, rb, playerPos, anim); break;

        case BossState::Charge:

            {
                // プレイヤーの方向ベクトルを計算して渡す
                Vector3 dir = isPlayerFound ? (playerPos - trans.GetWorldPosition()) : Vector3::Forward;
                dir.Normalize();
                UpdateCharge(dt, boss, trans, rb, dir, anim);
            }
            break;

        case BossState::SweepAttack: UpdateSweep(dt, boss, trans, playerPos, anim); break;

        case BossState::Summon: UpdateSummon(dt, boss, trans, anim); break;

        case BossState::Stun:
            // 膝つきモーションがあれば再生
            rb.velocity = Vector3::Zero;
            if (boss.stateTimer > 3.0f) {
                boss.state          = BossState::Idle;
                boss.actionCooldown = 1.0f;
            }
            break;

        case BossState::Death:
        // 死亡演出中の処理
        // 例えば倒れ込むアニメーションが終わったらフラグを立てるなど
        // 今回は簡易的に即時完了とみなす、あるいはタイマーで管理
        boss.isDeadAnimationFinished = true;
        return; // これ以降のAI処理はしない

        default: boss.state = BossState::Idle; break;
        }
    });
}



void BossBehaviorSystem::SpawnMinions(const Vector3 &centerPos)
{
    // 3体召喚
    for (int i = 0; i < 3; ++i) {
        float   angle = i * (3.14159f * 2.0f / 3.0f); // 120度ずつずらす
        Vector3 offset(sin(angle), 0, cos(angle));
        Vector3 spawnPos = centerPos + offset * 10.0f;

        // ランダムな一種類の雑魚敵をスポーンさせる
        Prefab::SpawnPrefab(*_world, "Assets/GamePlay/Prefabs/Enemy/EnemyPawn", spawnPos);
    }
}



// 行動決定ロジック（変更なし）
void BossBehaviorSystem::DecideNextAction(BossComponent &boss, float dist)
{
    if (boss.summonCooldown <= 0.0f) {
        boss.state = BossState::Summon;
        return;
    }
    if (dist < boss.attackRangeSweep) {
        boss.state = BossState::SweepAttack;
        return;
    }
    if (dist > 10.0f) {

        boss.state = BossState::Charge;
        return;
    }
    boss.state = BossState::Move;
}

// ---------------------------------------------------------
// Move (63f = 1.05s)
// ---------------------------------------------------------
void BossBehaviorSystem::UpdateMove(float dt,
    BossComponent                        &boss,
    TransformComponent                   &trans,
    RigidBodyComponent                   &rb,
    const Vector3                        &playerPos,
    AnimatorComponent                    &anim)
{

    // アニメーション再生 (Move)
    anim.Play(_animIdMove, true, 1.0f, 0.4f);

    Vector3 diff = playerPos - trans.GetWorldPosition();
    diff.y       = 0;
    if (diff.LengthSquared() > 0.01f) {
        diff.Normalize();
        float yaw      = atan2f(diff.x, diff.z);
        trans.rotation = Quaternion::Slerp(trans.rotation,
            Quaternion::CreateFromAxisAngle(Vector3::UnitY, yaw),
            dt * boss.rotationSpeed);
    }
    Vector3 forward = Vector3::Transform(Vector3::UnitZ, trans.rotation);
    rb.velocity     = forward * boss.moveSpeed;

    // 2秒程度歩いたら見直す
    if (boss.stateTimer > 2.0f || (playerPos - trans.GetWorldPosition()).Length() < 4.0f) {
        boss.state          = BossState::Idle;
        boss.actionCooldown = 0.2f; // 素早く次の行動へ
    }
}

// ---------------------------------------------------------
// Tackle (150f = 2.5s)
// ---------------------------------------------------------
void BossBehaviorSystem::UpdateCharge(float dt,
    BossComponent                          &boss,
    TransformComponent                     &trans,
    RigidBodyComponent                     &rb,
    const Vector3                          &dirToPlayer,
    AnimatorComponent                      &anim)
{

    // アニメーション再生 (Tackle, ループなし, 強制リセット)
    // stateTimerが0の瞬間だけ再生開始
    if (boss.stateTimer == 0.0f) {
        anim.Play(_animIdTackle, false, 1.0f, 0.1f, true);
        boss.hasHitTarget = false; // ★初期化: まだ誰にも当たっていない
    }
    else {
        // 継続再生確認（ForceResetしないPlayは「再生中なら何もしない」ので安全）
        anim.Play(_animIdTackle, false);
    }

    // ★修正: マジックナンバーを排除し、期間を加算してタイミングを決定
    float t_dash_start = boss.chargeWindupDur;
    float t_dash_end   = t_dash_start + boss.chargeDashDur;
    float t_anim_end   = t_dash_end + boss.chargeRecoveryDur;

    // 1. 予備動作
    if (boss.stateTimer < t_dash_start) {
        rb.velocity    = Vector3::Zero;
        float yaw      = atan2f(dirToPlayer.x, dirToPlayer.z);
        trans.rotation = Quaternion::Slerp(
            trans.rotation, Quaternion::CreateFromAxisAngle(Vector3::UnitY, yaw), dt * 5.0f);
    }
    // 2. 突進中
    else if (boss.stateTimer < t_dash_end) {
        if (!boss.hasActioned) {
            boss.hasActioned = true; // SE再生などに利用
        }
        Vector3 forward = Vector3::Transform(Vector3::UnitZ, trans.rotation);
        rb.velocity     = forward * boss.chargeSpeed;

        // ★追加: 突進の当たり判定 (体当たり)
        // まだヒットしていない場合のみ判定する
        if (!boss.hasHitTarget) {
            // プレイヤーを探す
            auto pView = _world->View<PlayerComponent,
                TransformComponent,
                HealthComponent,
                RigidBodyComponent>();
            for (auto pID : pView) {
                auto *pTrans  = _world->GetComponent<TransformComponent>(pID);
                auto *pHealth = _world->GetComponent<HealthComponent>(pID);
                auto *pRb     = _world->GetComponent<RigidBodyComponent>(pID);

                // 距離チェック (ボスの半径2.0mくらいと仮定)
                float distSq =
                    Vector3::DistanceSquared(trans.GetWorldPosition(), pTrans->GetWorldPosition());
                if (distSq < 2.0f * 2.0f) {
                    // ヒット！
                    //pHealth->TakeDamage(boss.damageCharge);

                    // 派手に吹き飛ばす
                    if (pRb) {
                        Vector3 knockback = forward * 20.0f;
                        knockback.y       = 5.0f; // 少し打ち上げ
                        pRb->velocity     = knockback;
                    }

                    boss.hasHitTarget = true; // この突進ではもう当たらない
                }
            }
        }

    }
    // 3. 硬直
    else if (boss.stateTimer < t_anim_end) {
        rb.velocity = Vector3::Lerp(rb.velocity, Vector3::Zero, dt * 10.0f);
    }
    // 終了
    else {
        boss.state          = BossState::Idle;
        boss.actionCooldown = 1.0f;
        boss.hasActioned    = false; // フラグはIdleに戻る時にリセットしても良い
    }
}

// ---------------------------------------------------------
// Slash (90f = 1.5s)
// ---------------------------------------------------------
void BossBehaviorSystem::UpdateSweep(float dt,
    BossComponent                         &boss,
    TransformComponent                    &trans,
    const Vector3                         &playerPos,
    AnimatorComponent                     &anim)
{

    // アニメーション再生 (Slash)
    if (boss.stateTimer == 0.0f)
        anim.Play(_animIdSlash, false, 1.0f, 0.1f, true);
    else
        anim.Play(_animIdSlash, false);



  // 攻撃判定発生タイミング
    if (boss.stateTimer >= boss.sweepHitTime && !boss.hasActioned) {
        boss.hasActioned = true;

        // ★追加: 扇状の当たり判定
        Vector3 bossPos = trans.GetWorldPosition();
        Vector3 bossFwd = Vector3::Transform(Vector3::UnitZ, trans.rotation);

        auto pView =
            _world
                ->View<PlayerComponent, TransformComponent, HealthComponent, RigidBodyComponent>();
        for (auto pID : pView) {
            auto *pTrans  = _world->GetComponent<TransformComponent>(pID);
            auto *pHealth = _world->GetComponent<HealthComponent>(pID);
            auto *pRb     = _world->GetComponent<RigidBodyComponent>(pID);

            Vector3 toPlayer = pTrans->GetWorldPosition() - bossPos;
            float   dist     = toPlayer.Length();

            // 1. 射程内か？
            if (dist < boss.attackRangeSweep) {
                // 正規化
                toPlayer /= dist;

                // 2. 角度チェック (内積)
                // 0.5 = cos(60度) => 左右60度、合計120度の扇形
                if (bossFwd.Dot(toPlayer) > 0.5f) {

                    // ダメージ適用
                    //pHealth->TakeDamage(boss.damageSweep);

                    // ノックバック (プレイヤーから見て後ろへ)
                    if (pRb) {
                        Vector3 knockback = toPlayer * 15.0f;
                        pRb->velocity     = pRb->velocity + knockback;
                    }
                }
            }
        }
    }

    if (boss.stateTimer > boss.sweepDuration) {
        boss.state          = BossState::Idle;
        boss.actionCooldown = 0.5f;
        boss.hasActioned    = false;
    }
}

// ---------------------------------------------------------
// Summon (120f = 2.0s)
// ---------------------------------------------------------
void BossBehaviorSystem::UpdateSummon(
    float dt, BossComponent &boss, TransformComponent &trans, AnimatorComponent &anim)
{

    // アニメーション再生 (Summon)
    if (boss.stateTimer == 0.0f)
        anim.Play(_animIdSummon, false, 1.0f, 0.1f, true);
    else
        anim.Play(_animIdSummon, false);


    if (boss.stateTimer >= boss.summonSpawnTime && !boss.hasActioned) {
        boss.hasActioned = true;
        SpawnMinions(trans.GetWorldPosition());
        boss.summonCooldown = 20.0f;
    }

    if (boss.stateTimer > boss.summonDuration) {
        boss.state          = BossState::Idle;
        boss.actionCooldown = 1.0f;
    }
}



// デバッグ描画
void BossBehaviorSystem::OnDrawDebug(ShapeRenderer *renderer)
{
   ForEach([&](BossComponent& boss, 
                TransformComponent& trans, 
                RigidBodyComponent& rb, 
                AnimatorComponent& anim, 
                const ModelComponent& model) {
            Vector3 pos = trans.GetWorldPosition();

            // ステート表示
            Vector4 color = Vector4(1, 1, 1, 1);
            switch (boss.state) {
            case BossState::Charge: color = Vector4(1, 0, 0, 1); break;
            case BossState::SweepAttack: color = Vector4(1, 0.5, 0, 1); break;
            //case BossState::Shockwave: color = Vector4(0, 0, 1, 1); break;
            case BossState::Summon: color = Vector4(0, 1, 1, 1); break;
            }
            renderer->DrawBox(pos, {0.0f, 0.0f, 0.0f}, Vector3(1, 2, 1), color);

            // 攻撃範囲
            if (boss.state == BossState::SweepAttack) {
                // 扇状の代わりに簡易シリンダー
                renderer->DrawWireframeCylinder(
                    pos, boss.attackRangeSweep, 1.0f, Vector4(1, 0, 0, 0.5f));
            }
            if (boss.state == BossState::Shockwave) {
                renderer->DrawWireframeCylinder(
                    pos, boss.attackRangeShock, 0.2f, Vector4(0, 0, 1, 0.5f));
            }
        });
}

REGISTER_LOGIC_SYSTEM(BossBehaviorSystem, Priority::LogicStage::Update);