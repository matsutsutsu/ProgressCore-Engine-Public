#include "InputFacade.h"
#include "InputKeyTable.h"
#include "Engine/Platform/Core/FileSystem.h"
#include <algorithm>

#include <json.hpp>
using json = nlohmann::json;

#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif

namespace
{
    // JSON の "Device" 文字列 -> デバイス種別。未知の文字列は Keyboard 扱い
    InputDeviceType StringToDeviceType(const std::string &str)
    {
        if (str == "Keyboard") return InputDeviceType::Keyboard;
        if (str == "Mouse_Button") return InputDeviceType::Mouse_Button;
        if (str == "GamePad_Button") return InputDeviceType::GamePad_Button;
        if (str == "GamePad_AxisLX") return InputDeviceType::GamePad_AxisLX;
        if (str == "GamePad_AxisLY") return InputDeviceType::GamePad_AxisLY;
        if (str == "GamePad_AxisRX") return InputDeviceType::GamePad_AxisRX;
        if (str == "GamePad_AxisRY") return InputDeviceType::GamePad_AxisRY;
        if (str == "GamePad_TriggerL") return InputDeviceType::GamePad_TriggerL;
        if (str == "GamePad_TriggerR") return InputDeviceType::GamePad_TriggerR;
        return InputDeviceType::Keyboard;
    }



    // JSON の "Key" 文字列 -> キーコード。キーの追加は InputKeyTable.h で行う
    unsigned int StringToKeyCode(const std::string &str)
    {
        return InputKeyTable::StringToKeyCode(str);
    }
}

// ---------------------------------------------------------------------------
// 読み込みと登録
// ---------------------------------------------------------------------------
bool InputFacade::LoadConfig(const std::string &filePath)
{
    const std::string text = FileSystem::ReadAllText(filePath);
    if (text.empty())
    {
        return false;
    }

    json doc;
    try
    {
        doc = json::parse(text);
    }
    catch (...)
    {
        return false;
    }

    // 既存の割り当てを捨てる（ホットリロードで読み直すため）
    m_actionBindings.clear();
    m_axisBindings.clear();
    m_actionTriggerTypes.clear();

    // 1. Actions の読み込み
    if (doc.contains("Actions"))
    {
        for (const auto &actionJson : doc["Actions"])
        {
            std::string name = actionJson["Name"].get<std::string>();

            // トリガー種別（なければ "Down"）
            std::string trig = actionJson.value("Trigger", std::string("Down"));
            SetActionTriggerType(name, StringToTriggerType(trig));

            for (const auto &bindJson : actionJson["Bindings"])
            {
                InputDeviceType device  = StringToDeviceType(bindJson["Device"].get<std::string>());
                unsigned int    keyCode = StringToKeyCode(bindJson["Key"].get<std::string>());
                AddActionBinding(name, device, keyCode);
            }
        }
    }

    // 2. Axes の読み込み
    if (doc.contains("Axes"))
    {
        for (const auto &axisJson : doc["Axes"])
        {
            std::string name = axisJson["Name"].get<std::string>();

            for (const auto &bindJson : axisJson["Bindings"])
            {
                InputDeviceType device  = StringToDeviceType(bindJson["Device"].get<std::string>());
                unsigned int    keyCode = StringToKeyCode(bindJson["Key"].get<std::string>());
                float           scale   = bindJson.value("Scale", 1.0f);
                AddAxisBinding(name, device, keyCode, scale);
            }
        }
    }

    return true;
}



void InputFacade::AddActionBinding(const std::string &actionName, InputDeviceType device, unsigned int keyCode)
{
    uint32_t hash = CCL::Utils::HashString(actionName);
    m_actionBindings[hash].push_back({device, keyCode, 1.0f});
}



void InputFacade::AddAxisBinding(const std::string &axisName, InputDeviceType device, unsigned int keyCode, float scale)
{
    uint32_t hash = CCL::Utils::HashString(axisName);
    m_axisBindings[hash].push_back({device, keyCode, scale});
}



void InputFacade::SetActionTriggerType(const std::string &actionName, TriggerType type)
{
    uint32_t hash              = CCL::Utils::HashString(actionName);
    m_actionTriggerTypes[hash] = type;
}



// ---------------------------------------------------------------------------
// 評価
// ---------------------------------------------------------------------------
bool InputFacade::EvalActionDown(const std::vector<InputBinding> &binds) const
{
    for (const auto &bind : binds)
    {
        switch (bind.deviceType)
        {
        case InputDeviceType::Keyboard:
            if (m_rawInput->GetKeyboard().IsDown(bind.keyCode)) return true;
            break;
        case InputDeviceType::Mouse_Button:
            if (m_rawInput->GetMouse().IsDown(bind.keyCode)) return true;
            break;
        case InputDeviceType::GamePad_Button:
            if (m_rawInput->GetGamePad().IsDown(bind.keyCode)) return true;
            break;
        default:
            break;
        }
    }
    return false;
}



bool InputFacade::EvalActionTriggered(const std::vector<InputBinding> &binds) const
{
    for (const auto &bind : binds)
    {
        switch (bind.deviceType)
        {
        case InputDeviceType::Keyboard:
            // ロジックは固定ステップで読むので、ClearLogicInput まで溜めた「押した瞬間」を見る
            if (m_rawInput->GetLogicKeyTriggered(bind.keyCode)) return true;
            break;
        case InputDeviceType::Mouse_Button:
            if ((m_rawInput->GetLogicMouseButtonDown() & bind.keyCode) != 0) return true;
            break;
        case InputDeviceType::GamePad_Button:
            if ((m_rawInput->GetLogicPadButtonDown() & bind.keyCode) != 0) return true;
            break;
        default:
            break;
        }
    }
    return false;
}



bool InputFacade::EvalActionReleased(const std::vector<InputBinding> &binds) const
{
    for (const auto &bind : binds)
    {
        switch (bind.deviceType)
        {
        case InputDeviceType::Keyboard:
            if (m_rawInput->GetKeyboard().IsReleased(bind.keyCode)) return true;
            break;
        case InputDeviceType::Mouse_Button:
            if (m_rawInput->GetMouse().IsReleased(bind.keyCode)) return true;
            break;
        case InputDeviceType::GamePad_Button:
            if (m_rawInput->GetGamePad().IsReleased(bind.keyCode)) return true;
            break;
        default:
            break;
        }
    }
    return false;
}



#ifdef ENABLE_EDITOR
namespace
{
    // エディタでウィジェットを操作している間か。その間はゲームの入力を止める（名前の入力中に WASD が漏れない）
    // WantCaptureMouse は見ない。GameView も ImGui のウィンドウなので、マウスを乗せただけで true になる
    inline bool EditorIsConsumingInput()
    {
        if (!ImGui::GetCurrentContext()) return false;
        return ImGui::GetIO().WantCaptureKeyboard || ImGui::IsAnyItemActive();
    }
}
#endif

bool InputFacade::GetAction(uint32_t actionHash) const
{
    if (!m_rawInput) return false;
#ifdef ENABLE_EDITOR
    // エディタで値調整中はゲーム側へ入力を通さない（プレイヤー暴発防止）
    if (EditorIsConsumingInput()) return false;
#endif
    auto it = m_actionBindings.find(actionHash);
    if (it == m_actionBindings.end()) return false;

    // 種別を引く（未登録なら Down）
    TriggerType type = TriggerType::Down;
    auto        tit  = m_actionTriggerTypes.find(actionHash);
    if (tit != m_actionTriggerTypes.end()) type = tit->second;

    switch (type)
    {
    case TriggerType::Triggered:
        return EvalActionTriggered(it->second);
    case TriggerType::Released:
        return EvalActionReleased(it->second);
    case TriggerType::Down:
    default:
        return EvalActionDown(it->second);
    }
}



float InputFacade::GetAxis(uint32_t axisHash) const
{
    if (!m_rawInput) return 0.0f;
#ifdef ENABLE_EDITOR
    // エディタで値調整中は軸入力を中立(0)にして移動の暴発を防ぐ
    if (EditorIsConsumingInput()) return 0.0f;
#endif
    auto it = m_axisBindings.find(axisHash);
    if (it == m_axisBindings.end()) return 0.0f;

    float finalValue = 0.0f;

    for (const auto &bind : it->second)
    {
        float rawValue = 0.0f;

        switch (bind.deviceType)
        {
        case InputDeviceType::Keyboard:
            if (m_rawInput->GetKeyboard().IsDown(bind.keyCode)) rawValue = 1.0f;
            break;
        case InputDeviceType::Mouse_Button:
            if (m_rawInput->GetMouse().IsDown(bind.keyCode)) rawValue = 1.0f;
            break;
        case InputDeviceType::GamePad_Button:
            if (m_rawInput->GetGamePad().IsDown(bind.keyCode)) rawValue = 1.0f;
            break;
        case InputDeviceType::GamePad_AxisLX:
            rawValue = m_rawInput->GetGamePad().GetAxisLX();
            break;
        case InputDeviceType::GamePad_AxisLY:
            rawValue = m_rawInput->GetGamePad().GetAxisLY();
            break;
        case InputDeviceType::GamePad_AxisRX:
            rawValue = m_rawInput->GetGamePad().GetAxisRX();
            break;
        case InputDeviceType::GamePad_AxisRY:
            rawValue = m_rawInput->GetGamePad().GetAxisRY();
            break;
        case InputDeviceType::GamePad_TriggerL:
            rawValue = m_rawInput->GetGamePad().GetTriggerL();
            break;
        case InputDeviceType::GamePad_TriggerR:
            rawValue = m_rawInput->GetGamePad().GetTriggerR();
            break;
        }

        finalValue += rawValue * bind.scale;
    }

    return std::clamp(finalValue, -1.0f, 1.0f);
}



// ---------------------------------------------------------------------------
// 振動
// ---------------------------------------------------------------------------

void InputFacade::SetVibration(float leftMotor, float rightMotor, float duration)
{
    // 直近の操作がゲームパッドのときだけ振動させる
    if (m_rawInput && GetActiveDeviceType() == ActiveDeviceType::GamePad)
    {
        m_rawInput->GetGamePad().SetVibration(leftMotor, rightMotor, duration);
    }
}