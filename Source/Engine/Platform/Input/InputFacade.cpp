#include "InputFacade.h"
#include <algorithm>

// =======================================================
// 辞書への登録
// =======================================================
void InputFacade::AddActionBinding(const std::string& actionName, InputDeviceType device, unsigned int keyCode) {
    m_actionBindings[actionName].push_back({ device, keyCode, 1.0f });
}

void InputFacade::AddAxisBinding(const std::string& axisName, InputDeviceType device, unsigned int keyCode, float scale) {
    m_axisBindings[axisName].push_back({ device, keyCode, scale });
}

// =======================================================
// デジタル入力（Action）の取得
// 割り当てられたデバイスのうち、どれか1つでも条件を満たせば true
// =======================================================
bool InputFacade::GetAction(const std::string& actionName) const {
    if (!m_rawInput) return false;
    auto it = m_actionBindings.find(actionName);
    if (it == m_actionBindings.end()) return false;

    for (const auto& bind : it->second) {
        switch (bind.deviceType) {
        case InputDeviceType::Keyboard:       if (m_rawInput->GetKeyboard().IsDown(bind.keyCode)) return true; break;
        case InputDeviceType::Mouse_Button:   if (m_rawInput->GetMouse().IsDown(bind.keyCode)) return true; break;
        case InputDeviceType::GamePad_Button: if (m_rawInput->GetGamePad().IsDown(bind.keyCode)) return true; break;
        default: break; // Actionは通常、ボタン系のデバイスのみを対象とする
        }
    }
    return false;
}

bool InputFacade::GetActionTriggered(const std::string& actionName) const {
    if (!m_rawInput) return false;
    auto it = m_actionBindings.find(actionName);
    if (it == m_actionBindings.end()) return false;

    for (const auto& bind : it->second) {
        switch (bind.deviceType) {
        case InputDeviceType::Keyboard:       if (m_rawInput->GetKeyboard().IsTriggered(bind.keyCode)) return true; break;
        case InputDeviceType::Mouse_Button:   if (m_rawInput->GetMouse().IsTriggered(bind.keyCode)) return true; break;
        case InputDeviceType::GamePad_Button: if (m_rawInput->GetGamePad().IsTriggered(bind.keyCode)) return true; break;
        default: break;
        }
    }
    return false;
}

bool InputFacade::GetActionReleased(const std::string& actionName) const {
    if (!m_rawInput) return false;
    auto it = m_actionBindings.find(actionName);
    if (it == m_actionBindings.end()) return false;

    for (const auto& bind : it->second) {
        switch (bind.deviceType) {
        case InputDeviceType::Keyboard:       if (m_rawInput->GetKeyboard().IsReleased(bind.keyCode)) return true; break;
        case InputDeviceType::Mouse_Button:   if (m_rawInput->GetMouse().IsReleased(bind.keyCode)) return true; break;
        case InputDeviceType::GamePad_Button: if (m_rawInput->GetGamePad().IsReleased(bind.keyCode)) return true; break;
        default: break;
        }
    }
    return false;
}

// =======================================================
// アナログ入力（Axis）の取得
// 割り当てられたデバイスの入力を合算（加算）し、-1.0～1.0 にクランプして返す
// ※Wキーを押せば +1.0、Sキーを押せば -1.0 となるように変換する
// =======================================================
float InputFacade::GetAxis(const std::string& axisName) const {
    if (!m_rawInput) return 0.0f;
    auto it = m_axisBindings.find(axisName);
    if (it == m_axisBindings.end()) return 0.0f;

    float finalValue = 0.0f;

    for (const auto& bind : it->second) {
        float rawValue = 0.0f;

        switch (bind.deviceType) {
            // --- デジタルボタンをアナログ軸として扱う（W/A/S/Dキーでの移動など） ---
        case InputDeviceType::Keyboard:
            if (m_rawInput->GetKeyboard().IsDown(bind.keyCode)) rawValue = 1.0f;
            break;
        case InputDeviceType::Mouse_Button:
            if (m_rawInput->GetMouse().IsDown(bind.keyCode)) rawValue = 1.0f;
            break;
        case InputDeviceType::GamePad_Button:
            if (m_rawInput->GetGamePad().IsDown(bind.keyCode)) rawValue = 1.0f;
            break;

            // --- 純粋なアナログ値の取得 ---
        case InputDeviceType::GamePad_AxisLX: rawValue = m_rawInput->GetGamePad().GetAxisLX(); break;
        case InputDeviceType::GamePad_AxisLY: rawValue = m_rawInput->GetGamePad().GetAxisLY(); break;
        case InputDeviceType::GamePad_AxisRX: rawValue = m_rawInput->GetGamePad().GetAxisRX(); break;
        case InputDeviceType::GamePad_AxisRY: rawValue = m_rawInput->GetGamePad().GetAxisRY(); break;
        case InputDeviceType::GamePad_TriggerL: rawValue = m_rawInput->GetGamePad().GetTriggerL(); break;
        case InputDeviceType::GamePad_TriggerR: rawValue = m_rawInput->GetGamePad().GetTriggerR(); break;
        }

        // scale（重み・反転）を掛けて合算
        finalValue += rawValue * bind.scale;
    }

    // -1.0f ～ 1.0f の範囲に制限（Wとパッドを両方同時に倒しても速度が2倍にならないようにする）
    return std::clamp(finalValue, -1.0f, 1.0f);
}