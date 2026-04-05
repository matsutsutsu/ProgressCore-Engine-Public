#include "PlayerCarInputSystem.h"

#include "ECS/Core/CCL_Chunk.h"
#include "Engine/Platform/Input/Input.h"

// システムの実行順序の定義ヘッダー
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

using namespace CCL::ECS;

PlayerCarInputSystem::PlayerCarInputSystem()
    : IfSystem("PlayerCarInputSystem")
{
}

void PlayerCarInputSystem::Update(float dt)
{
    // 1. 世界から Input データ (Resource) を受け取る
    if (!_world->HasResource<Input*>()) return;
    auto* input = _world->GetResource<Input*>();
    if (!input) return;

    auto& pad = input->GetGamePad();
    auto& kb = input->GetKeyboard();



    ForEach([&](PlayerCarComponent& car) {
        car.input.throttle = 0.0f;
        car.input.steering = 0.0f;
        car.input.jump = false;
        });

    

    // Logic用の蓄積入力を取得
    unsigned int logicPadDown = input->GetPadButtonDownLogic();

    ForEach([&](PlayerCarComponent& car) {
        float throttle = 0.0f;
        float steering = 0.0f;

        // キーボード入力の取得
        bool keyA = kb.IsDown('A');
        bool keyD = kb.IsDown('D');
        bool keyW = kb.IsDown('W');
        bool keyS = kb.IsDown('S');

        // ハンドル (AとD) 同時押し対策
        if (keyA && keyD) {
            steering = 0.0f;
        }
        else if (keyA) {
            steering = -1.0f; // 左
        }
        else if (keyD) {
            steering = 1.0f;  // 右
        }
        else {
            steering = pad.GetAxisLX();
        }

        // アクセル・ブレーキ (WとS) 同時押し対策
        if (keyW && keyS) {
            throttle = 0.0f;
        }
        else if (keyW) {
            throttle = 1.0f;  // 前進
        }
        else if (keyS) {
            throttle = -1.0f; // 後退
        }
        else {
            throttle = pad.GetAxisLY();
        }

        // 抽象化された input 構造体にマッピング
        car.input.throttle = throttle;
        car.input.steering = steering;
        car.input.jump = (logicPadDown & GamePad::BTN_A);
        });
}

REGISTER_LOGIC_SYSTEM(PlayerCarInputSystem, Priority::LogicStage::L01_Input);