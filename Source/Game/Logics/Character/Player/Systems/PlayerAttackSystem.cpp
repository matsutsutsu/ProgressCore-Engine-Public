#include "PlayerAttackSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

// トリガーのハッシュ値を事前計算
static const uint32_t HASH_TRIG_ATTACK = CCL::Utils::HashString("Attack");

void PlayerAttackSystem::Update(float dt)
{
    ForEach([&](const CharacterMovementInputComponent &inputData,
                AnimParametersComponent               &animParams,
                PlayerComponent                       &player) {
        // 前フレームで押されておらず、今フレームで押された瞬間（エッジ検出）
        if (inputData.isAttackRequested && !player.wasAttackButtonPressed)
        {
            // ==============================================================
            // ★ コンボ・先行入力バッファの登録
            // "Attack" トリガーを、0.25秒間（約15フレーム）だけ掲示板に貼り付ける。
            // ==============================================================
            animParams.SetTrigger(HASH_TRIG_ATTACK, 0.25f);

            // 将来、スタミナ消費や攻撃不可状態（スタン等）の判定を追加する場合は
            // ここのif文の中で評価し、条件を満たした時のみSetTriggerを呼ぶように拡張できる。
        }

        // 次のフレームのエッジ検出のために状態を保存
        player.wasAttackButtonPressed = inputData.isAttackRequested;
    });
}

// 実行タイミング：PlayerInputSystem(L01_Input)の直後、AnimControllerSystem(L02_Update)より前
REGISTER_LOGIC_SYSTEM(PlayerAttackSystem, Priority::LogicStage::L01_Input);