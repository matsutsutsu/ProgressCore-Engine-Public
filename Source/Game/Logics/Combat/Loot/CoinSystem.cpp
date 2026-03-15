#include "CoinSystem.h"
#include "Game/Logics/Character/Player/PlayerComponent.h" // プレイヤー特定用
#include "Engine/GamePlay/Core/Scene/SceneManager.h"
#include <cmath> // std::sqrt用
#include <DirectXMath.h>

#include "Game/Logics/GameEvent/GameEvents.h"
#include "Game/Logics/Character/Enemy/EnemyComponent.h"
#include "Engine/GamePlay/Graphics/Core/ModelComponent.h"
#include "Engine/GamePlay/Graphics/Core/MaterialComponent.h"

// システムの実行順序の定義ヘッダー
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"


using namespace CCL::ECS;
using namespace DirectX;

CoinSystem::CoinSystem() : IfSystem("CoinSystem") {}

void CoinSystem::Initialize()
{
    // _world がまだセットされていなければ何もしない（安全対策）
    if (!_world) return;

    // 「誰かが死んだ」というイベントが飛んできたら、この処理を実行する
    _world->GetEventBus().Subscribe<EntityKilledEvent>([this](const EntityKilledEvent &e) {
        if (e.isPlayer || e.droppedCoinAmount <= 0) return;

        //_world->Destroy(e.killedEntityID); // 敵のエンティティを削除（必要なら）

        // 手紙に書いてある金額をそのままスポーンするだけ
        SpawnCoin(e.droppedCoinAmount, e.position);
    });
}

void CoinSystem::Update(float dt)
{
    // 1. プレイヤーの位置を取得
    // ECSでは特定のエンティティを探す際、Viewを使ってコンポーネントで検索するのが定石です
    XMFLOAT3 playerPos   = {0, 0, 0};
    bool     playerFound = false;

    // PlayerComponentを持つエンティティ一覧を取得
    auto players = _world->View<PlayerComponent, TransformComponent>();
    if (!players.empty()) {
        // 最初に見つかったプレイヤーを対象とする（通常は1人）
        auto *pTrans = _world->GetComponent<TransformComponent>(players[0]);
        if (pTrans) {
            playerPos = pTrans->GetWorldPosition(); // ローカル位置＝ワールド位置と仮定（親がいない場合）
            // 親がいる場合は pTrans->GetWorldPosition() を使うのが安全ですが、
            // 今回は頻繁に呼ぶため直接参照で高速化を優先するか、TransformUpdate後ならGetWorldPosition()でもOK
            playerFound = true;
        }
    }

    // 2. コインの更新処理
    // IfSystemのForEachWithIDを使って、CoinとTransformを持つエンティティを走査
    ForEachWithID([&](EntityID            entityID,
                      CoinComponent      &coin,
                      TransformComponent &trans,
                      RigidBodyComponent &rb) {
        // --- 寿命処理 ---
        coin.lifeTime -= dt;
        if (coin.lifeTime <= 0.0f) {
            _world->Destroy(entityID); // 寿命切れで削除
            return;                                 // このエンティティの処理は終了
        }

        // --- アニメーションタイマー更新 (★追加) ---
        coin.animTimer += dt;

        if (!playerFound) return;

        // --- 距離計算 (XMFLOAT3の手動計算で依存を減らす) ---
        float dx     = playerPos.x - trans.position.x;
        float dy     = playerPos.y - trans.position.y;
        float dz     = playerPos.z - trans.position.z;
        float distSq = dx * dx + dy * dy + dz * dz;

        // --- 吸い寄せ判定 ---
        // まだ吸い寄せモードでなく、かつ範囲内に入ったらモードON
        if (!coin.isMagnetized && distSq < (coin.magnetRange * coin.magnetRange)) {
            coin.isMagnetized = true;
        }

        // --- 挙動の分岐 ---
        if (coin.isMagnetized) {
            // 吸い寄せ中: 物理挙動を乗っ取る
            rb.useGravity = false; // 重力を切る（プレイヤーへ一直線に向かわせるため）

            // プレイヤーへの方向
            float dist = std::sqrt(distSq);
            if (dist < 0.001f) dist = 0.001f;

            // 速度を強制的に書き換える（加速させながら）
            // 既存の速度に足し込むと慣性が残りすぎるので、誘導弾のように少し強引に向かせる
            float speed = coin.magnetPower;

            // 単純なセット:
            // rb.velocity = { (dx/dist)*speed, (dy/dist)*speed, (dz/dist)*speed };

            // もしくは補間して滑らかに:
            XMFLOAT3 targetVel = {(dx / dist) * speed, (dy / dist) * speed, (dz / dist) * speed};
            float    lerpRate  = 5.0f * dt;
            rb.velocity.x += (targetVel.x - rb.velocity.x) * lerpRate;
            rb.velocity.y += (targetVel.y - rb.velocity.y) * lerpRate;
            rb.velocity.z += (targetVel.z - rb.velocity.z) * lerpRate;

            // 取得判定
            if (distSq < 0.5f * 0.5f) {
                SceneManager::Instance().GetPlayerData().money += coin.amount;
                _world->Destroy(entityID);
            }

            // 吸い寄せ中は回転を早めると面白いかも（オプション）
            // coin.rotationSpeed = 360.0f;
        }
        else {
            // 【物理モード（ばらけ中）】
            // 基本的な移動・重力は PhysicsIntegrationSystem に任せる！
            rb.useGravity = true;

            // ★表現A: Y軸回転 (くるくる)
            // 現在のクォータニオンを取得
            XMVECTOR currentRot = XMLoadFloat4(&trans.rotation);
            // Y軸周りの回転を作成 (角度はラジアン変換)
            XMVECTOR deltaRot = XMQuaternionRotationAxis(
                XMVectorSet(0, 1, 0, 0), XMConvertToRadians(coin.rotationSpeed * dt));
            // 回転を合成して適用
            XMStoreFloat4(&trans.rotation, XMQuaternionMultiply(currentRot, deltaRot));

            // ★ここだけCoinSystemが介入：「バウンド処理」
            // PhysicsSystemは「Yが0より下なら0に戻す」ことしかしないので、
            // ここで「地面近くで、かつ下に向かっているなら、速度を反転させる」処理をする

            if (trans.position.y <= GROUND_LEVEL && rb.velocity.y < 0.0f) {
                // 速度反転（跳ね返り）
                rb.velocity.y = -rb.velocity.y * coin.bounce;

                // 横滑りの摩擦（地面に当たった時だけ減速）
                rb.velocity.x *= coin.friction;
                rb.velocity.z *= coin.friction;

                // 勢いが弱まったらY速度を完全停止（微振動防止）
                if (rb.velocity.y < 2.0f) {
                    rb.velocity.y = 0.0f;

                    // 接地した瞬間のYを基準にする (浮遊用)
                    coin.baseY = trans.position.y;
                }
            }

            // 上下ふわふわ (Floating)
            // ほぼ静止している（接地している）時だけふわふわさせる
            if (std::abs(rb.velocity.y) < 0.1f && std::abs(rb.velocity.x) < 0.1f &&
                std::abs(rb.velocity.z) < 0.1f) {
                rb.useGravity = false;     // ふわふわ中は重力を切る
                rb.velocity   = {0, 0, 0}; // 速度もゼロに

                // sin波でY座標をオフセットさせる
                float floatOffset = std::sin(coin.animTimer * coin.floatSpeed) * coin.floatRange;
                // 基準Y + オフセットの位置に設定（物理移動ではなく直接配置）
                trans.position.y = coin.baseY + floatOffset + GROUND_LEVEL;
            }

        }
    });
}


void CoinSystem::SpawnCoin(int amount, const DirectX::XMFLOAT3 &spawnPos)
{
    auto archetype = CCL::ECS::ArchetypeHelper::Generate<TransformComponent,
        CoinComponent,
        ModelComponent,
        MaterialComponent,
        RigidBodyComponent>();

    auto coinID = _world->RequestSpawnEntity(archetype);

    TransformComponent trans;
    trans.position = spawnPos;
    trans.position.y += 1.0f;
    trans.scale = {0.08f, 0.08f, 0.08f};
    _world->AddComponent<TransformComponent>(coinID, trans);

    CoinComponent coin;
    coin.amount = amount;
    _world->AddComponent<CoinComponent>(coinID, coin);

    ModelComponent modelComp("Assets/Models/Object/Coin.glb");
    _world->AddComponent<ModelComponent>(coinID, modelComp);

    RigidBodyComponent rb;
    rb.mass            = 1.0f;
    rb.useGravity      = true;
    rb.bounciness      = 0.6f;
    rb.linearDamping   = 0.98f;
    rb.dynamicFriction = 0.5f;

    float randX = (static_cast<float>(rand()) / RAND_MAX) * 10.0f - 5.0f;
    float randZ = (static_cast<float>(rand()) / RAND_MAX) * 10.0f - 5.0f;
    float randY = (static_cast<float>(rand()) / RAND_MAX) * 5.0f + 5.0f;

    rb.velocity = {randX, randY, randZ};
    _world->AddComponent<RigidBodyComponent>(coinID, rb);
}


REGISTER_LOGIC_SYSTEM(CoinSystem, Priority::LogicStage::Resolution);