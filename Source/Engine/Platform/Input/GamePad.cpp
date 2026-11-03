#include <windows.h>
#include <Xinput.h>
#include "GamePad.h"
#include <algorithm>
#include <cmath>
#include <tracy/Tracy.hpp>

#pragma comment(lib, "xinput.lib")

void GamePad::Update(float dt)
{
    ZoneScopedN("GamePad::Update");

    axisLx = axisLy = 0.0f;
    axisRx = axisRy = 0.0f;
    triggerL = triggerR = 0.0f;

    GamePadButton newButtonState = 0;

    // 未接続なら全入力 0 のまま
    XINPUT_STATE xinputState;
    if (XInputGetState(slot, &xinputState) == ERROR_SUCCESS)
    {
        XINPUT_GAMEPAD &pad = xinputState.Gamepad;

        if (pad.wButtons & XINPUT_GAMEPAD_DPAD_UP) newButtonState |= BTN_UP;
        if (pad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) newButtonState |= BTN_RIGHT;
        if (pad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) newButtonState |= BTN_DOWN;
        if (pad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) newButtonState |= BTN_LEFT;
        if (pad.wButtons & XINPUT_GAMEPAD_A) newButtonState |= BTN_A;
        if (pad.wButtons & XINPUT_GAMEPAD_B) newButtonState |= BTN_B;
        if (pad.wButtons & XINPUT_GAMEPAD_X) newButtonState |= BTN_X;
        if (pad.wButtons & XINPUT_GAMEPAD_Y) newButtonState |= BTN_Y;
        if (pad.wButtons & XINPUT_GAMEPAD_START) newButtonState |= BTN_START;
        if (pad.wButtons & XINPUT_GAMEPAD_BACK) newButtonState |= BTN_BACK;
        if (pad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) newButtonState |= BTN_LEFT_THUMB;
        if (pad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) newButtonState |= BTN_RIGHT_THUMB;
        if (pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) newButtonState |= BTN_LEFT_SHOULDER;
        if (pad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) newButtonState |= BTN_RIGHT_SHOULDER;
        if (pad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) newButtonState |= BTN_LEFT_TRIGGER;
        if (pad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) newButtonState |= BTN_RIGHT_TRIGGER;

        // スティックは X/Y 両方がデッドゾーン内のときだけ 0 にする（斜め入力を削らないため）
        if ((pad.sThumbLX < XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE && pad.sThumbLX > -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) &&
            (pad.sThumbLY < XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE && pad.sThumbLY > -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE))
        {
            pad.sThumbLX = 0;
            pad.sThumbLY = 0;
        }

        if ((pad.sThumbRX < XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE && pad.sThumbRX > -XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE) &&
            (pad.sThumbRY < XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE && pad.sThumbRY > -XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE))
        {
            pad.sThumbRX = 0;
            pad.sThumbRY = 0;
        }

        triggerL = static_cast<float>(pad.bLeftTrigger) / 255.0f;
        triggerR = static_cast<float>(pad.bRightTrigger) / 255.0f;
        axisLx   = static_cast<float>(pad.sThumbLX) / static_cast<float>(0x8000);
        axisLy   = static_cast<float>(pad.sThumbLY) / static_cast<float>(0x8000);
        axisRx   = static_cast<float>(pad.sThumbRX) / static_cast<float>(0x8000);
        axisRy   = static_cast<float>(pad.sThumbRY) / static_cast<float>(0x8000);

        newButtonState |= StickToButtons(buttonState[0]);
    }

    // ボタン状態の更新
    {
        buttonState[1] = buttonState[0];
        buttonState[0] = newButtonState;

        buttonDown = ~buttonState[1] & newButtonState; // 押した瞬間
        buttonUp   = ~newButtonState & buttonState[1]; // 離した瞬間
    }

    // 振動の残り時間を進め、切れたらモーターを止める
    if (m_vibrationTimer > 0.0f)
    {
        m_vibrationTimer -= dt;
        if (m_vibrationTimer <= 0.0f)
        {
            m_vibrationTimer = 0.0f;
            m_currentLeft    = 0.0f;
            m_currentRight   = 0.0f;
            ApplyVibrationHardware(0.0f, 0.0f); // モーター停止
        }
    }
}

GamePadButton GamePad::StickToButtons(GamePadButton prev) const
{
    // 押す境目と離す境目を分ける。1つの境目だと、その近くで指が震えただけで押し直しが続き、選択が何段も飛ぶ
    constexpr float kPress   = 0.6f;  // この傾きを超えたら押したことにする
    constexpr float kRelease = 0.35f; // この傾きを下回るまで押したままにする

    const bool  horizontal = std::abs(axisLx) >= std::abs(axisLy); // 斜めは強く倒した方の向きだけにする
    const float amount     = horizontal ? axisLx : axisLy;

    // 今の向き（XInput の Y は上が正）
    GamePadButton dir = 0;
    if (horizontal)
    {
        dir = (amount > 0.0f) ? BTN_LSTICK_RIGHT : BTN_LSTICK_LEFT;
    }
    else
    {
        dir = (amount > 0.0f) ? BTN_LSTICK_UP : BTN_LSTICK_DOWN;
    }

    // 前から押している向きなら、離す境目までは押したまま
    const float threshold = (prev & dir) ? kRelease : kPress;
    return (std::abs(amount) > threshold) ? dir : 0;
}

void GamePad::SetVibration(float left, float right, float duration)
{
    if (left >= m_currentLeft || right >= m_currentRight || duration >= m_vibrationTimer)
    {
        m_currentLeft    = left;
        m_currentRight   = right;
        m_vibrationTimer = duration;
        ApplyVibrationHardware(left, right);
    }
}

void GamePad::ApplyVibrationHardware(float left, float right)
{
    ZoneScopedN("GamePad::ApplyVibrationHardware");

    // 範囲外の値が来ても WORD への変換で溢れないよう 0..1 に丸める
    left  = std::clamp(left, 0.0f, 1.0f);
    right = std::clamp(right, 0.0f, 1.0f);

    XINPUT_VIBRATION vibration;
    ZeroMemory(&vibration, sizeof(XINPUT_VIBRATION));

    // 0..1 を WORD（0..65535）に変換する
    // 左モーター：低周波（重い・鈍い振動）
    vibration.wLeftMotorSpeed = static_cast<WORD>(left * 65535.0f);
    // 右モーター：高周波（軽い・鋭い振動）
    vibration.wRightMotorSpeed = static_cast<WORD>(right * 65535.0f);

    XInputSetState(slot, &vibration);
}