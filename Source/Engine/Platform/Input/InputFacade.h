#pragma once
#include "IInputAPI.h"
#include "Input.h"
#include <unordered_map>

class InputFacade : public IInputAPI {
private:
    Input* m_rawInput; // 生のデバイス入力基盤

    // 辞書データ（アクション名 -> 複数の割り当てリスト）
    std::unordered_map<std::string, std::vector<InputBinding>> m_actionBindings;
    std::unordered_map<std::string, std::vector<InputBinding>> m_axisBindings;

public:
    InputFacade(Input* rawInput) : m_rawInput(rawInput) {}
    virtual ~InputFacade() = default;

    void AddActionBinding(const std::string& actionName, InputDeviceType device, unsigned int keyCode) override;
    void AddAxisBinding(const std::string& axisName, InputDeviceType device, unsigned int keyCode, float scale = 1.0f) override;

    bool GetAction(const std::string& actionName) const override;
    bool GetActionTriggered(const std::string& actionName) const override;
    bool GetActionReleased(const std::string& actionName) const override;
    float GetAxis(const std::string& axisName) const override;
};