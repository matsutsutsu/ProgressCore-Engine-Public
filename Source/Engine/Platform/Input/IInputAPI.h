#pragma once
#include <string>
#include <vector>

// 1. 入力デバイスの種類
enum class InputDeviceType {
    Keyboard,         // キーボード
    Mouse_Button,     // マウスボタン
    GamePad_Button,   // ゲームパッドのボタン
    GamePad_AxisLX,   // 左スティックX
    GamePad_AxisLY,   // 左スティックY
    GamePad_AxisRX,   // 右スティックX
    GamePad_AxisRY,   // 右スティックY
    GamePad_TriggerL, // 左トリガー
    GamePad_TriggerR  // 右トリガー
};

// 2. マッピング（割り当て）の情報
struct InputBinding {
    InputDeviceType deviceType;
    unsigned int    keyCode;    // キーボードのVK_Wや、GamePad::BTN_Aなどのビットフラグ
    float           scale = 1.0f; // 軸の方向や感度（Wなら1.0、Sなら-1.0等）
};

// 3. 純粋な入力API
class IInputAPI {
public:
    virtual ~IInputAPI() = default;

    // --- 設定（バインディング） ---
    virtual void AddActionBinding(const std::string& actionName, InputDeviceType device, unsigned int keyCode) = 0;
    virtual void AddAxisBinding(const std::string& axisName, InputDeviceType device, unsigned int keyCode, float scale = 1.0f) = 0;

    // --- デジタル入力（0 か 1） ---
    virtual bool GetAction(const std::string& actionName) const = 0;
    virtual bool GetActionTriggered(const std::string& actionName) const = 0;
    virtual bool GetActionReleased(const std::string& actionName) const = 0;

    // --- アナログ入力（-1.0f ～ 1.0f） ---
    virtual float GetAxis(const std::string& axisName) const = 0;
};