#include "Game/Logics/Combat/Weapon/Core/GunBuilder.h"
#include "ECS/Common/CCL_Common.h"
#include "ECS/Core/CCL_World.h"
#include "Game/Logics/Combat/Weapon/Core/BulletContextComponent.h"
 
#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Graphics/Core/Graphics.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Engine/GamePlay/Physics/RigidBody/RigidBodyComponent.h"
#include "Engine/GamePlay/Utils/TimerComponent.h"
#include "Engine/GamePlay/Graphics/Particle/GPUParticleComponent.h"
#include "Engine/GamePlay/Physics/Collision/SphereColliderComponent.h"
#include "Engine/GamePlay/Graphics/Core/MaterialComponent.h"
#include "Engine/GamePlay/Core/NameComponent.h"

#include "Engine/Serialization/SceneSerializer.h" // 汎用シリアライザ

#include "Game/Utils/BulletPoolManager.h"
#include "Game/Utils/PooledBulletComponent.h"

#include "Engine/Core/Math/StringHash.h"

#include "Game/Logics/Combat/Weapon/Attachments/AttachmentComponents.h"
#include "Game/Logics/Combat/Weapon/Attachments/BulletHitStateComponent.h"
#include "Game/Logics/Combat/Weapon/Attachments/AttachmentRegistry.h"

#include "Game/Logics/Combat/Weapon/Core/BulletBlueprintBuilder.h" // Step2で作ったもの
#include "Game/Logics/Combat/Weapon/Core/BulletBlueprintLibrary.h" // Step4で作ったもの

#ifdef TRACY_ENABLE
#include "tracy/Tracy.hpp"
#endif


// ===================================================================================
// 実装: GunBuilder
// 役割: 弾丸Entityの生成と初期化
//
// 処理の流れ:
//   1. Spawnフェーズのエフェクト適用（弾数や初速の決定）
//   2. アーキタイプに基づいたEntity生成
//   3. BulletContextComponentへのエフェクト情報のコピー
//   4. RigidBody（物理）とTransform（位置・回転）の初期計算
// ===================================================================================


// ---------------------------------------------------------------------------
// Blueprintの解決 (作成 or キャッシュ取得)
// ---------------------------------------------------------------------------
const BulletBlueprint *GunBuilder::ResolveBlueprint(Core::World *world)
{
#ifdef TRACY_ENABLE
    ZoneScopedN("GunBuilder::ResolveBlueprint (Spike Warning!)"); 
#endif

    // 1. キャッシュが生きていればそれを返す
    if (_cachedBlueprint && !_isDirty) {
        return _cachedBlueprint;
    }

    // 2. ライブラリの取得 (なければ作成)
    if (!world->HasResource<BulletBlueprintLibrary>()) {
        world->AddResource<BulletBlueprintLibrary>();
    }
    auto &library = world->GetResource<BulletBlueprintLibrary>();

    // 3. Builderを使って設計図を作成
    BulletBlueprintBuilder builder;

    // 基本ステータスの設定
    builder.SetBaseStats(baseDamage, baseLifeTime)
        .SetCritical(criticalChance, criticalMultiplier) // ★ご要望の機能
        .SetKnockback(knockbackPower);                   // ★ご要望の機能

    // エフェクト（モッド）の注入
    // 以前の vector<EffectID> をビットパッキングされた設計図に変換します
    for (auto mod : spawnMods) builder.AddEffect(BulletPhase::Spawn  , mod.effect  ,mod.level);
    for (auto mod : travelMods) builder.AddEffect(BulletPhase::Travel, mod.effect  ,mod.level);
    for (auto mod : onHitMods) builder.AddEffect(BulletPhase::OnHit  , mod.effect  ,mod.level);
    for (auto mod : endMods) builder.AddEffect(BulletPhase::End      , mod.effect  ,mod.level);

    // 必要ならパラメータ設定 (例: Splitモッドがあれば分裂数を設定など)
    // builder.SetSplitParams(3);

    // 4. ビルド & ライブラリ登録
    _cachedBlueprint = library.Register(builder.Build());

    // 1. まず必須の基本パーツだけを入れた「金型(Archetype)」を用意する
    _cachedArchetype = ArchetypeHelper::Generate<TransformComponent,
        RigidBodyComponent,
        SphereColliderComponent,
        BulletContextComponent,
        NameComponent,
        TimerComponent,
        BulletHitStateComponent>();

    _activeBindings.clear(); // 使うマニュアルのページをまとめるファイル

    // レジストリ（全種類）を走査するのは、この「設計図更新時」の1回だけ！
    
    // アタッチメントのアーキタイプを最初に作っておき
    // 発射時に中身のデータを入れるだけにする
    const auto &allBindings = AttachmentRegistry::GetBindings();
    for (const auto &binding : allBindings) {
        
        // 3. 設計図を見て、このマニュアルのモッドが組み込まれているか(1個以上あるか)を確認する
        if (_cachedBlueprint->GetEffectCount(binding.phase, binding.effect) > 0) {

            // 4. 組み込まれているなら、金型にそのパーツの穴(型)を追加する！
            binding.addToArchetype(_cachedArchetype);

            // 5. そして、発射時にすぐ使えるように、このページに付箋を貼っておく(キャッシュ)
            // 発射時に「このモッドが何レベルあるか？」をすぐに確認できるように、バインディングへのポインタを保存しておきます。
            _activeBindings.push_back(&binding);
        }
    }

    // 新しい設計図が完成したので、Dirtyフラグを解除する
    _isDirty         = false;

    return _cachedBlueprint;
}



// ---------------------------------------------------------------------------
// 発射処理 (Fire)
// ---------------------------------------------------------------------------
void GunBuilder::Fire(Core::World *world, DirectX::XMFLOAT3 startPos, DirectX::XMFLOAT3 direction)
{
    using namespace DirectX;

    #ifdef TRACY_ENABLE
    ZoneScopedN("GunBuilder::Fire"); 
    #endif

    // 1. 設計図を用意
    const BulletBlueprint *bp = ResolveBlueprint(world);

    // =======================================================================
    // 1. Spawnフェーズの適用 (EffectLibrary呼び出し)
    // =======================================================================
    int      actualBulletCount = baseBulletCount;
    float    actualSpread      = baseSpread;
    float    actualSpeed       = baseSpeed;
    int      actualPierce      = 0; // Pierceなどの加算用
    XMFLOAT3 actualDir         = direction;

    // Contextを作ってLibraryに渡す
    SpawnContext spawnCtx{
        actualBulletCount, actualSpread, actualSpeed, actualPierce,
        actualDir, startPos, world
    };

    // Blueprintの中身を見て、登録されているSpawnモッドの数だけ適用する (ハードコードゼロ)
    for (const auto &spawnBind : AttachmentRegistry::GetSpawnBindings()) {
        if (int lv = bp->GetEffectCount(BulletPhase::Spawn, spawnBind.effect)) {
            spawnBind.applySpawn(lv, spawnCtx);
        }
    }

    // =======================================================================
    // 2. Entity生成準備
    // =======================================================================


    // プールマネージャー
    if (!world->HasResource<BulletPoolManager>()) {
        world->AddResource<BulletPoolManager>();
    }
    auto       &poolMgr = world->GetResource<BulletPoolManager>();

    // 文字列ではなくコンパイル時ハッシュを使用する
    // これにより、毎フレームの無駄な文字列生成と検索コストがゼロになります
    uint32_t poolKeyHash = CCL::Utils::HashString("Player_Standard_Bullet");

    // 3. 生成ループ
    for (int i = 0; i < actualBulletCount; ++i) {
        // Entity生成 (プール or 新規)
        // ハッシュ値でプールから取得
        // 先ほど作った金型(_cachedArchetype)を使って、プールから空の弾丸を取り出す
        BulletPoolResult result = poolMgr.WakeOrSpawn(*world, poolKeyHash, _cachedArchetype);
        EntityID         id     = result.id;

        // =========================================================
        // 【1】コンポーネントの完全リセット
        // =========================================================
        if (result.isNewlyCreated) {
            // --- 新規作成時は AddComponent で領域と初期値を作る ---
            BulletContextComponent ctx;
            ctx.blueprint     = bp;
            ctx.lifeTime      = bp->baseLifeTime;
            ctx.currentDamage = bp->baseDamage;
            ctx.isCritical    = false;
            ctx.pierceCount   = actualPierce;
            ctx.reflectCount  = bp->GetEffectCount(BulletPhase::Travel, EffectID::Reflection);
            ctx.selfID        = id;
            ctx.markForEnd    = false;
            ctx.hitHistory.Clear();
            world->AddComponent<BulletContextComponent>(id, ctx);

            RigidBodyComponent rb;
            rb.velocity =
                CalculateVelocity(direction, actualSpeed, actualSpread, i, actualBulletCount);
            rb.useGravity         = false;
            rb.ignoreMapCollision = true;
            rb.maxSpeed           = 999.0f;
            world->AddComponent<RigidBodyComponent>(id, rb);

            TransformComponent trans;
            XMVECTOR           posVec = XMLoadFloat3(&startPos);
            XMVECTOR           dirVec = XMLoadFloat3(&direction);
            dirVec                    = XMVector3Normalize(dirVec);
            XMStoreFloat3(&trans.position, posVec + (dirVec * spawnOffset));
            trans.position.y += 1.0f;

            if (abs(rb.velocity.x) > 0.001f || abs(rb.velocity.z) > 0.001f) {
                float    yaw  = atan2f(rb.velocity.x, rb.velocity.z);
                XMVECTOR rotQ = XMQuaternionRotationRollPitchYaw(0, yaw, 0);
                XMStoreFloat4(&trans.rotation, rotQ);
            }
            else {
                trans.rotation = XMFLOAT4(0, 0, 0, 1);
            }
            world->AddComponent<TransformComponent>(id, trans);

            SphereColliderComponent col;
            col.radius = 0.5f;
            col.layer  = Layer::Bullet;
            col.mask   = Layer::Enemy | Layer::Wall | Layer::EnemyBullet;
            world->AddComponent<SphereColliderComponent>(id, col);

            BulletHitStateComponent hitState;
            hitState.Reset(); // 初期状態を保証
            world->AddComponent<BulletHitStateComponent>(id, hitState);
        }
        else {
            // --- 再利用時は GetComponent で取得して値だけ書き換える（リンク情報を壊さない） ---
            if (auto *ctx = world->GetComponent<BulletContextComponent>(id)) {
                ctx->blueprint     = bp;
                ctx->lifeTime      = bp->baseLifeTime;
                ctx->currentDamage = bp->baseDamage;
                ctx->isCritical    = false;
                ctx->pierceCount   = actualPierce;
                ctx->reflectCount  = bp->GetEffectCount(BulletPhase::Travel, EffectID::Reflection);
                ctx->selfID        = id;
                ctx->markForEnd    = false;
                ctx->hitHistory.Clear();
            }

            if (auto *rb = world->GetComponent<RigidBodyComponent>(id)) {
                rb->velocity =
                    CalculateVelocity(direction, actualSpeed, actualSpread, i, actualBulletCount);
                rb->useGravity         = false;
                rb->ignoreMapCollision = true;
                rb->maxSpeed           = 999.0f;
            }

            if (auto *trans = world->GetComponent<TransformComponent>(id)) {
                XMVECTOR posVec = XMLoadFloat3(&startPos);
                XMVECTOR dirVec = XMLoadFloat3(&direction);
                dirVec          = XMVector3Normalize(dirVec);
                XMStoreFloat3(&trans->position, posVec + (dirVec * spawnOffset));
                trans->position.y += 1.0f;

                auto *rb = world->GetComponent<RigidBodyComponent>(id);
                if (rb && (abs(rb->velocity.x) > 0.001f || abs(rb->velocity.z) > 0.001f)) {
                    float    yaw  = atan2f(rb->velocity.x, rb->velocity.z);
                    XMVECTOR rotQ = XMQuaternionRotationRollPitchYaw(0, yaw, 0);
                    XMStoreFloat4(&trans->rotation, rotQ);
                }
                else {
                    trans->rotation = XMFLOAT4(0, 0, 0, 1);
                }
            }

            if (auto *col = world->GetComponent<SphereColliderComponent>(id)) {
                col->radius      = 0.5f;
                col->layer       = Layer::Bullet;
                col->mask        = Layer::Enemy | Layer::Wall | Layer::EnemyBullet;
                col->isColliding = false; // 古い衝突フラグを消す
                col->lastHitEntity = InvalidEntityID; // 古いヒット情報を消す
            }

            if (auto *hitState = world->GetComponent<BulletHitStateComponent>(id)) {
                hitState->Reset(); // 過去のヒット履歴や死亡フラグを完全にリセット
            }
        }


        // --- アタッチメントデータの自動注入 ---
        // _activeBindings には、この設計図に「確実に存在している」モッドのリストだけが入っている
        for (const auto *binding : _activeBindings) {

            // 1. 設計図から「何個付いているか(レベル)」を取り出す (例: 2個)
            int lv = bp->GetEffectCount(binding->phase, binding->effect);

            // 2. マニュアルに書いてある「データの入れ方」をそのまま実行する！
            // 該当するコンポーネントに、レベルに応じた初期値をセットしてEntityにくっつける
            binding->injectData(world, id, lv, bp);
        }

        // =========================================================
        // 【2】最大の改善ポイント: パーティクルの使い回しロジック
        // =========================================================
        if (result.isNewlyCreated) {
            // --- A. 新規作成時 ---
            // 初回のみパーティクルを生成し、弾丸の子供として取り付ける
            Prefab::SpawnParticle(*world,
                "Assets/GamePlay/Prefabs/Particles/VFX_Weapon_Bullet.json",
                id,
                DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
        }
        else {
            // --- B. プールからの再利用時 ---
            // パーティクルは「すでに子供としてくっついている」ため、生成やSetParentは不要。
            // ただし、そのまま発射すると「前の着弾地点から新しい発射地点まで、光の線がワープして繋がってしまう」
            // ため、GPU側の時間とエミッタ位置をリセットする。

            if (auto *trans = world->GetComponent<TransformComponent>(id)) {

                // ==========================================================
                // 最重要：行列の強制即時更新
                // 後の TransformUpdateSystem を待たず、今ここで親のワールド行列を確定させる
                // ==========================================================
                trans->UpdateMatrix();
                DirectX::XMMATRIX parentMatrix = DirectX::XMLoadFloat4x4(&trans->worldMatrix);

                EntityID childID = trans->firstChildID;

                while (childID != 0) {

                    // ==========================================================
                    // 最重要：子供の行列の強制即時伝播
                    // これを行わないと、直後のGPUParticleSystemが地下の座標を読んでしまう
                    // ==========================================================
                    if (auto *childTrans = world->GetComponent<TransformComponent>(childID)) {
                        childTrans->UpdateMatrix(parentMatrix);
                    }

                    if (auto *gpuP = world->GetComponent<GPUParticleComponent>(childID)) {
                        gpuP->config.time = 0.0f;
                        // これをtrueにすることで、次のフレームでInitCSが走り、
                        // 過去のパーティクルの軌跡が断ち切られて現在位置から出直す
                        gpuP->needsGpuInit = true;
                    }

                    // 次の兄弟（他のパーティクルやモデルがあれば）へ
                    if (auto *childTrans = world->GetComponent<TransformComponent>(childID)) {
                        childID = childTrans->nextSiblingID;
                    }
                    else {
                        break;
                    }
                }
            }
        }

      

    }
}

void GunBuilder::EnemyFire(
    CCL::ECS::Core::World *world, DirectX::XMFLOAT3 startPos, DirectX::XMFLOAT3 direction)
{
    //using namespace DirectX;

    //// 1. 【Spawn Phase】 の処理
    //// Spawnモッドは「弾が生まれる前」に、弾の数や初速を決定する
    //int   bulletCount = baseBulletCount;
    //float spread      = baseSpread;
    //float speed       = baseSpeed;
    //float pierceCount;


    //// コンテキスト作成
    //EffectSpawnContext spawnCtx{
    //    bulletCount, spread, speed, pierceCount, direction, spawnMods, world, startPos};

    //// Spawn時の初期設定をBulletEffectLibraryでそれぞれに合った関数を呼び出す
    //for (EffectID mod : spawnMods) {
    //    BulletEffectLibrary::ApplySpawn(mod, spawnCtx);
    //}

    //// 事前にアーキタイプを作成（ループ内で毎回生成しない最適化）
    //static const Archetype arch = ArchetypeHelper::Generate<TransformComponent,
    //    RigidBodyComponent,
    //    MaterialComponent,
    //    SphereColliderComponent,
    //    BulletContextComponent,
    //    NameComponent>();

    //// 2. 弾の生成ループ
    //for (int i = 0; i < bulletCount; ++i) {
    //    // Entity生成リクエスト
    //    EntityID id = world->RequestSpawnEntity(arch);

    //    // --- Component A: Context (脳みそ) ---
    //    BulletContextComponent ctx;
    //    //ctx.spawnEffects  = spawnMods;
    //    //ctx.travelEffects = travelMods;
    //    //ctx.onHitEffects  = onHitMods;
    //    //ctx.endEffects    = endMods;

    //    // 貫通回数をセット
    //    ctx.pierceCount = 1; // 必要なら個数分加算するロジックへ変更可

    //    // ★追加: selfID をセットする
    //    ctx.selfID = id;

    //    world->AddComponent<BulletContextComponent>(id, ctx);

    //    // auto model = std::make_shared<Model>(Graphics::Instance().GetDevice(),
    //    // "Assets/Models/Shape/Sphere.glb"); world->AddComponent<ModelComponent>(id,
    //    // ModelComponent(model));

    //    // --- Component B: RigidBody (物理) ---
    //    RigidBodyComponent rb;
    //    // 拡散計算 (bulletCountを渡して中央揃えにする)
    //    rb.velocity   = CalculateVelocity(direction, speed, spread, i, bulletCount);
    //    rb.useGravity = false; // 弾は重力無視が一般的（必要ならtrue）
    //    rb.linearDamping = 1.0f;  // 減衰なし
    //    // 弾丸はマップ物理（壁ずり）を無視し、RaycastSystemに判定を任せる
    //    rb.ignoreMapCollision = true;

    //    world->AddComponent<RigidBodyComponent>(id, rb);

    //    // --- Component C: Transform (位置) ---

    //    TransformComponent trans;

    //    // 1. 位置の計算 (startPos + direction * offset)
    //    XMVECTOR posVec = XMLoadFloat3(&startPos);
    //    XMVECTOR dirVec = XMLoadFloat3(&direction);
    //    dirVec          = XMVector3Normalize(dirVec); // 安全のため正規化

    //    // 向いている方向に spawnOffset 分だけ進める
    //    XMVECTOR offsetPos = posVec + (dirVec * spawnOffset);

    //    XMStoreFloat3(&trans.position, offsetPos);
    //    trans.position.y += 1.0f;

    //    // 2. 回転の計算 (弾の進行方向に向ける)
    //    // RigidBodyの速度ベクトル(=進行方向)を取得
    //    // ※ループの前半で rb.velocity を計算済みである前提
    //    XMVECTOR velocityVec = XMLoadFloat3(&rb.velocity);

    //    // 速度がほぼ0でない場合のみ回転を計算
    //    if (XMVectorGetX(XMVector3LengthSq(velocityVec)) > 0.001f) {
    //        velocityVec = XMVector3Normalize(velocityVec);

    //        // Y軸回転(Yaw)を求める (トップビューゲーム想定)
    //        float yaw = atan2f(XMVectorGetX(velocityVec), XMVectorGetZ(velocityVec));

    //        // クォータニオン作成 (Y軸回転)
    //        XMVECTOR rotQ = XMQuaternionRotationRollPitchYaw(0, yaw, 0);
    //        XMStoreFloat4(&trans.rotation, rotQ);
    //    }
    //    else {
    //        // 動かないなら単位元
    //        trans.rotation = DirectX::XMFLOAT4(0, 0, 0, 1);
    //    }

    //    world->AddComponent<TransformComponent>(id, trans);

    //    // --- Component D: Collider (衝突) ---
    //    SphereColliderComponent col;
    //    col.radius = 0.5f;                       // デフォルトサイズ
    //    col.layer  = Layer::EnemyBullet;              // 自分のレイヤータイプ
    //    col.mask   = Layer::Player | Layer::Wall | Layer::Bullet; // 当たってほしいレイヤータイプ

    //    world->AddComponent<SphereColliderComponent>(id, col);

    //    // TODO TimerComponentを弾丸に追加（仮）
    //    TimerComponent timer;
    //    world->AddComponent<TimerComponent>(id, timer);

    //    NameComponent nameComp("EnemyBullet");
    //    world->AddComponent<NameComponent>(id, nameComp);


    //    
    //           Prefab::SpawnParticle(*world,
    //        "Assets/GamePlay/Prefabs/Particles/VFX_Env_Fire.json",
    //        id,
    //        DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));

    //}
}

DirectX::XMFLOAT3 GunBuilder::CalculateVelocity(DirectX::XMFLOAT3 baseDir, float speed, float totalSpreadDeg, int index, int totalCount)
{
    using namespace DirectX;

    // 1. ベース方向を正規化
    XMVECTOR dirVec = XMLoadFloat3(&baseDir);
    dirVec = XMVector3Normalize(dirVec);

    // 2. 角度の計算（中央揃え）
    // 例: Count=3, Spread=30 -> angles: -15, 0, +15
    float angleDeg = 0.0f;
    if (totalCount > 1) {
        float startAngle = -totalSpreadDeg * 0.5f; // 左端
        float step = totalSpreadDeg / (float)(totalCount - 1); // 1個あたりの刻み幅
        angleDeg = startAngle + (step * index);
    }
    // Count=1の場合は角度0（直進）

    // 3. 回転行列（またはクォータニオン）の作成
    // ここではY軸（高さ）を軸にして左右に振る（トップビュー前提）
    XMVECTOR rotationQ = XMQuaternionRotationRollPitchYaw(0, XMConvertToRadians(angleDeg), 0);

    // 4. 回転適用と速度スケーリング
    XMVECTOR finalDir = XMVector3Rotate(dirVec, rotationQ);
    XMVECTOR finalVelocity = finalDir * speed;

    // 5. 書き出し
    XMFLOAT3 result;
    XMStoreFloat3(&result, finalVelocity);
    return result;
}
