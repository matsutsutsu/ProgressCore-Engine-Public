#include "PlayerShootingSystem.h"

// 実装詳細でのみ必要なインクルード
#include "Game/Logics/Combat/HealthComponent.h"
#include "Game/Logics/Combat/DeadTag.h"

#include <SimpleMath.h>
#include "Game/UI/PlayerHUD.h"

// システムの実行順序の定義ヘッダー
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"


using namespace CCL::ECS;
using namespace DirectX;
using namespace DirectX::SimpleMath;

PlayerShootingSystem::PlayerShootingSystem() : IfSystem("PlayerShootingSystem") {}

void PlayerShootingSystem::Update(float dt)
{
    // ★黒板（Resource）を取得する
    // まだ作られていなければ作る
    if (!_world->HasResource<PlayerHUDData>()) {
        _world->AddResource<PlayerHUDData>();
    }

    ForEachWithID([&](EntityID            id,
                      const PlayerComponent &player,
                      const TransformComponent &trans,
                      GunComponent       &gunComp,
                      AnimatorComponent  &animator,
                      const ModelComponent      &modelComp
                      /*AudioComponent     &audioComp*/) {
        // そもそもHPコンポーネントを取得しなくても、「死んでいる(Tag::DeadTagがある)」かどうかが分かります
        if (_world->HasComponent<Tag::DeadTag>(id)) {
            return; // 死んでいたら何もさせない（アニメーションも更新しない）
        }


        auto &hudData = _world->GetResource<PlayerHUDData>();

        // 0. ID取得 (初回のみ)
        if (modelComp.modelPtr && _animIdAttack == -1) {
            _animIdAttack = modelComp.modelPtr->GetAnimationIndex("Attack");
            // "Attack"がなければ "Shoot" など別の名前も試すと良いかもしれません
        }

        // 1. クールダウン消化
        if (gunComp.coolDownTimer > 0.0f) {
            gunComp.coolDownTimer -= dt;
        }

        // =======================================================
        // チャージ割合を計算して、黒板に書き込む
        // 撃った直後(timerが最大)は 0.0、撃てる状態(timerが0)なら 1.0 になる計算
        // =======================================================
        float ratio = 1.0f;
        if (gunComp.fireInterval > 0.0f) {
            float currentTimer = (std::max)(0.0f, gunComp.coolDownTimer);
            ratio              = 1.0f - (currentTimer / gunComp.fireInterval);
        }
        hudData.intervalRatio = ratio;

        // 2. 攻撃入力チェック (PlayerInputSystemで更新された値を見る)
        if (player.input.attack) {

            // 撃てる状態なら発射
            if (gunComp.coolDownTimer <= 0.0f) {
                Fire(gunComp, trans);
                gunComp.coolDownTimer = gunComp.fireInterval; // タイマーリセット


                // --- ★音を鳴らす処理 ---
                // 名前を設定して再生フラグを立てる
                //audioComp.name   = AUDIO_PLAYER_FIRE; // "PlayerFire"
                //audioComp.isPlay = true;              // これで次のAudioSystemの更新で音が鳴る
                // プレイヤーの射撃音は3Dにする必要はないかも（自分自身の音なので）
                // audioComp.is3D = false;
                // -----------------------


                // UI：撃った瞬間にゲージを空（0.0）にする
                hudData.intervalRatio = 0.0f;

                // ★アニメーション再生
                // Attackモーションがあれば再生 (ループなし, 速度1.0, ブレンド0.1秒,
                // 強制リセットあり)
                if (_animIdAttack != -1) {
                    // forceReset=true
                    // にすることで連打したときに毎回最初から再生されるようにする
                    animator.Play(_animIdAttack, false, 1.0f, 0.1f, true);
                }
            }
        }
    });
}

void PlayerShootingSystem::Fire(GunComponent &gunComp, const TransformComponent &shooterTrans)
{
#ifdef TRACY_ENABLE
    ZoneScopedN("PlayerShootingSystem::Fire"); 
#endif

    // ★修正: ローカル座標(position)ではなくワールド座標を使用
    // これにより、プレイヤーが動く床や乗り物に乗っていても正しい位置から発射されます。
    Vector3 worldPos = shooterTrans.GetWorldPosition();
    Vector3 forward  = shooterTrans.GetForward();
    Vector3 up       = shooterTrans.GetUp();

    // 発射位置：プレイヤーの中心から少し前(1.5m)、少し上(0.5m)
    // rotationを使った複雑な計算をせずとも、GetForward/GetUpで直感的に記述できます
    Vector3 spawnPos = worldPos + (forward * 1.5f) + (up * 0.5f);

    // GunBuilderのFireを実行
    // _world は SystemBase が持っているメンバ変数です
    gunComp.gun.Fire(_world, spawnPos, forward);
}

REGISTER_LOGIC_SYSTEM(PlayerShootingSystem, Priority::LogicStage::Update);