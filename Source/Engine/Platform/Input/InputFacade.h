#pragma once
#include "IInputAPI.h"
#include "Input.h"
#include <unordered_map>
#include <string>

class InputFacade : public IInputAPI {
private:
    Input* m_rawInput; // 生のデバイス入力基盤

    // 辞書データ（アクション名 -> 複数の割り当てリスト）
    std::unordered_map<uint32_t, std::vector<InputBinding>> m_actionBindings;
    std::unordered_map<uint32_t, std::vector<InputBinding>> m_axisBindings;

    // アクションごとの発火種別（未登録は Down 扱い）
    std::unordered_map<uint32_t, TriggerType> m_actionTriggerTypes;

public:
    // UI用：現在のアクティブなデバイスを取得する
    ActiveDeviceType GetActiveDeviceType() const override {
        return m_rawInput ? m_rawInput->GetActiveDeviceType() : ActiveDeviceType::KeyboardMouse;
    }

    // GUI用：現在登録されている全アクション名を取得
    const std::vector<std::string>& GetActionNames() const { return m_actionNames; }
    const std::vector<std::string>& GetAxisNames() const { return m_axisNames; }
private:
    std::vector<std::string> m_actionNames;
    std::vector<std::string> m_axisNames;

    // --- 内部評価ヘルパー（種別ごとの実処理） ---
    bool EvalActionDown(const std::vector<InputBinding>& binds) const;
    bool EvalActionTriggered(const std::vector<InputBinding>& binds) const;
    bool EvalActionReleased(const std::vector<InputBinding>& binds) const;

public:
    InputFacade(Input* rawInput) : m_rawInput(rawInput) {}
    virtual ~InputFacade() = default;

    // 外部JSONファイルからバインディングを読み込む
    bool LoadConfig(const std::string& filePath) override;

    float GetMouseX() const override {
        return static_cast<float>(m_rawInput->GetMouse().GetPositionX());
    }
    float GetMouseY() const override {
        return static_cast<float>(m_rawInput->GetMouse().GetPositionY());
    }
    float GetScreenWidth() const override {
        return static_cast<float>(m_rawInput->GetMouse().GetScreenWidth());
    }
    float GetScreenHeight() const override {
        return static_cast<float>(m_rawInput->GetMouse().GetScreenHeight());
    }

    void AddActionBinding(const std::string& actionName, InputDeviceType device, unsigned int keyCode) override;
    void AddAxisBinding(const std::string& axisName, InputDeviceType device, unsigned int keyCode, float scale = 1.0f) override;
    void SetActionTriggerType(const std::string& actionName, TriggerType type) override;

    // エディタ種別で評価する統一窓口
    bool GetAction(uint32_t actionHash) const override;
    float GetAxis(uint32_t axisHash) const override;

    void SetVibration(float leftMotor, float rightMotor, float duration) override;
};