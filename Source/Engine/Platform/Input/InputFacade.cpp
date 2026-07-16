#include "InputFacade.h"
#include "InputKeyTable.h"   // 共通キーテーブル（StringToKeyCode 委譲用）
#include <algorithm>
#include <fstream>
#include <iostream>

// ※お使いのJSONライブラリに合わせてください
#include <json.hpp>
using json = nlohmann::json;

#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif

namespace {
    // ---------------------------------------------------------
    // ヘルパー: 文字列 -> デバイスタイプ変換
    // ---------------------------------------------------------
    InputDeviceType StringToDeviceType(const std::string& str) {
        if (str == "Keyboard") return InputDeviceType::Keyboard;
        if (str == "Mouse_Button") return InputDeviceType::Mouse_Button;
        if (str == "GamePad_Button") return InputDeviceType::GamePad_Button;
        if (str == "GamePad_AxisLX") return InputDeviceType::GamePad_AxisLX;
        if (str == "GamePad_AxisLY") return InputDeviceType::GamePad_AxisLY;
        if (str == "GamePad_AxisRX") return InputDeviceType::GamePad_AxisRX;
        if (str == "GamePad_AxisRY") return InputDeviceType::GamePad_AxisRY;
        if (str == "GamePad_TriggerL") return InputDeviceType::GamePad_TriggerL;
        if (str == "GamePad_TriggerR") return InputDeviceType::GamePad_TriggerR;
        return InputDeviceType::Keyboard; // Default
    }

    // ---------------------------------------------------------
    // ヘルパー: 文字列 -> キーコード変換
    //   共通テーブル(InputKeyTable)へ委譲。
    //   キー定義の追加は InputKeyTable.h 側で行うこと。
    // ---------------------------------------------------------
    unsigned int StringToKeyCode(const std::string& str) {
        return InputKeyTable::StringToKeyCode(str);
    }
}

// =======================================================
// JSONファイルからのロード処理
// =======================================================
bool InputFacade::LoadConfig(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        return false;
    }

    json doc;
    try {
        file >> doc;
    } catch (...) {
        return false;
    }

    // 既存のバインディングをクリア（再読み込みに対応するため）
    m_actionBindings.clear();
    m_axisBindings.clear();
    m_actionTriggerTypes.clear();
    m_actionNames.clear();
    m_axisNames.clear();

    // 1. Actions の読み込み
    if (doc.contains("Actions")) {
        for (const auto& actionJson : doc["Actions"]) {
            std::string name = actionJson["Name"].get<std::string>();

            // トリガー種別（無ければ "Down"）
            std::string trig = actionJson.value("Trigger", std::string("Down"));
            SetActionTriggerType(name, StringToTriggerType(trig));

            for (const auto& bindJson : actionJson["Bindings"]) {
                InputDeviceType device = StringToDeviceType(bindJson["Device"].get<std::string>());
                unsigned int keyCode = StringToKeyCode(bindJson["Key"].get<std::string>());
                AddActionBinding(name, device, keyCode);
            }
        }
    }

    // 2. Axes の読み込み
    if (doc.contains("Axes")) {
        for (const auto& axisJson : doc["Axes"]) {
            std::string name = axisJson["Name"].get<std::string>();

            for (const auto& bindJson : axisJson["Bindings"]) {
                InputDeviceType device = StringToDeviceType(bindJson["Device"].get<std::string>());
                unsigned int keyCode = StringToKeyCode(bindJson["Key"].get<std::string>());
                float scale = bindJson.value("Scale", 1.0f);
                AddAxisBinding(name, device, keyCode, scale);
            }
        }
    }

    return true;
}

// =======================================================
// 辞書への登録 (文字列をハッシュ化して登録する)
// =======================================================
void InputFacade::AddActionBinding(const std::string& actionName, InputDeviceType device, unsigned int keyCode) {
    uint32_t hash = CCL::Utils::HashString(actionName);
    m_actionNames.push_back(actionName);
    m_actionBindings[hash].push_back({ device, keyCode, 1.0f });
}

void InputFacade::AddAxisBinding(const std::string& axisName, InputDeviceType device, unsigned int keyCode, float scale) {
    uint32_t hash = CCL::Utils::HashString(axisName);
    if (std::find(m_axisNames.begin(), m_axisNames.end(), axisName) == m_axisNames.end()) {
        m_axisNames.push_back(axisName);
    }
    m_axisBindings[hash].push_back({ device, keyCode, scale });
}

void InputFacade::SetActionTriggerType(const std::string& actionName, TriggerType type) {
    uint32_t hash = CCL::Utils::HashString(actionName);
    m_actionTriggerTypes[hash] = type;
}

// =======================================================
// 内部評価ヘルパー（種別ごとの実処理）
// =======================================================
bool InputFacade::EvalActionDown(const std::vector<InputBinding>& binds) const {
    for (const auto& bind : binds) {
        switch (bind.deviceType) {
        case InputDeviceType::Keyboard:       if (m_rawInput->GetKeyboard().IsDown(bind.keyCode)) return true; break;
        case InputDeviceType::Mouse_Button:   if (m_rawInput->GetMouse().IsDown(bind.keyCode)) return true; break;
        case InputDeviceType::GamePad_Button: if (m_rawInput->GetGamePad().IsDown(bind.keyCode)) return true; break;
        default: break;
        }
    }
    return false;
}

bool InputFacade::EvalActionTriggered(const std::vector<InputBinding>& binds) const {
    for (const auto& bind : binds) {
        switch (bind.deviceType) {
        case InputDeviceType::Keyboard:
            // 毎フレームの蓄積トリガーを参照（取りこぼし防止）
            if (m_rawInput->GetLogicKeyTriggered(bind.keyCode)) return true;
            break;
        case InputDeviceType::Mouse_Button:
            if ((m_rawInput->GetLogicMouseButtonDown() & bind.keyCode) != 0) return true;
            break;
        case InputDeviceType::GamePad_Button:
            if ((m_rawInput->GetLogicPadButtonDown() & bind.keyCode) != 0) return true;
            break;
        default: break;
        }
    }
    return false;
}

bool InputFacade::EvalActionReleased(const std::vector<InputBinding>& binds) const {
    for (const auto& bind : binds) {
        switch (bind.deviceType) {
        case InputDeviceType::Keyboard:       if (m_rawInput->GetKeyboard().IsReleased(bind.keyCode)) return true; break;
        case InputDeviceType::Mouse_Button:   if (m_rawInput->GetMouse().IsReleased(bind.keyCode)) return true; break;
        case InputDeviceType::GamePad_Button: if (m_rawInput->GetGamePad().IsReleased(bind.keyCode)) return true; break;
        default: break;
        }
    }
    return false;
}

#ifdef ENABLE_EDITOR
namespace {
    // ---------------------------------------------------------
    // B案: エディタでウィジェットを能動操作している間はゲーム入力を殺す。
    //   ・WantCaptureKeyboard : テキスト/数値欄にフォーカスがあり、キー入力を
    //                           ImGui が奪っている（名前入力中の WASD 漏れ防止）
    //   ・IsAnyItemActive     : スライダー/DragFloat 等をドラッグ中＝値調整中
    //   ※ GameView 自体が ImGui ウィンドウのため WantCaptureMouse は
    //     「画面にマウスを乗せただけ」で true になり通常プレイを阻害する。
    //     ゆえにマウスフラグは意図的に見ない。
    // ---------------------------------------------------------
    inline bool EditorIsConsumingInput() {
        if (!ImGui::GetCurrentContext()) return false;
        return ImGui::GetIO().WantCaptureKeyboard || ImGui::IsAnyItemActive();
    }
}
#endif

// =======================================================
// アクション取得（統一窓口）
//   エディタで設定された種別に従って評価する。
// =======================================================
bool InputFacade::GetAction(uint32_t actionHash) const {
    if (!m_rawInput) return false;
#ifdef ENABLE_EDITOR
    // エディタで値調整中はゲーム側へ入力を通さない（プレイヤー暴発防止）
    if (EditorIsConsumingInput()) return false;
#endif
    auto it = m_actionBindings.find(actionHash);
    if (it == m_actionBindings.end()) return false;

    // 種別を引く（未登録なら Down）
    TriggerType type = TriggerType::Down;
    auto tit = m_actionTriggerTypes.find(actionHash);
    if (tit != m_actionTriggerTypes.end()) type = tit->second;

    switch (type) {
    case TriggerType::Triggered: return EvalActionTriggered(it->second);
    case TriggerType::Released:  return EvalActionReleased(it->second);
    case TriggerType::Down:
    default:                     return EvalActionDown(it->second);
    }
}

// =======================================================
// アナログ入力（Axis）の取得 (ハッシュで最速検索)
// =======================================================
float InputFacade::GetAxis(uint32_t axisHash) const {
    if (!m_rawInput) return 0.0f;
#ifdef ENABLE_EDITOR
    // エディタで値調整中は軸入力を中立(0)にして移動の暴発を防ぐ
    if (EditorIsConsumingInput()) return 0.0f;
#endif
    auto it = m_axisBindings.find(axisHash);
    if (it == m_axisBindings.end()) return 0.0f;

    float finalValue = 0.0f;

    for (const auto& bind : it->second) {
        float rawValue = 0.0f;

        switch (bind.deviceType) {
        case InputDeviceType::Keyboard:       if (m_rawInput->GetKeyboard().IsDown(bind.keyCode)) rawValue = 1.0f; break;
        case InputDeviceType::Mouse_Button:   if (m_rawInput->GetMouse().IsDown(bind.keyCode)) rawValue = 1.0f; break;
        case InputDeviceType::GamePad_Button: if (m_rawInput->GetGamePad().IsDown(bind.keyCode)) rawValue = 1.0f; break;
        case InputDeviceType::GamePad_AxisLX: rawValue = m_rawInput->GetGamePad().GetAxisLX(); break;
        case InputDeviceType::GamePad_AxisLY: rawValue = m_rawInput->GetGamePad().GetAxisLY(); break;
        case InputDeviceType::GamePad_AxisRX: rawValue = m_rawInput->GetGamePad().GetAxisRX(); break;
        case InputDeviceType::GamePad_AxisRY: rawValue = m_rawInput->GetGamePad().GetAxisRY(); break;
        case InputDeviceType::GamePad_TriggerL: rawValue = m_rawInput->GetGamePad().GetTriggerL(); break;
        case InputDeviceType::GamePad_TriggerR: rawValue = m_rawInput->GetGamePad().GetTriggerR(); break;
        }

        finalValue += rawValue * bind.scale;
    }

    return std::clamp(finalValue, -1.0f, 1.0f);
}

void InputFacade::SetVibration(float leftMotor, float rightMotor, float duration) {
    // アクティブなデバイスがGamePadの時のみ振動させる
    if (m_rawInput && GetActiveDeviceType() == ActiveDeviceType::GamePad) {
        m_rawInput->GetGamePad().SetVibration(leftMotor, rightMotor, duration);
    }
}