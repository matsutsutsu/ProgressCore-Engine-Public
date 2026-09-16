#pragma once
#include "IInputAPI.h"
#include "Input.h"
#include <unordered_map>
#include <string>

class InputFacade : public IInputAPI
{
private:
    Input *m_rawInput; // 生のデバイス入力基盤

    // 辞書データ（アクション名 -> 複数の割り当てリスト）
    std::unordered_map<uint32_t, std::vector<InputBinding>> m_actionBindings;
    std::unordered_map<uint32_t, std::vector<InputBinding>> m_axisBindings;

    // アクションごとの発火種別（未登録は Down 扱い）
    std::unordered_map<uint32_t, TriggerType> m_actionTriggerTypes;

    // ゲームビューにフォーカスがある/全画面のときだけマウス視点を通す（既定 true）。
    bool m_gameInputActive = true;

public:
    // UI用：現在のアクティブなデバイスを取得する
    ActiveDeviceType GetActiveDeviceType() const override
    {
        return m_rawInput ? m_rawInput->GetActiveDeviceType() : ActiveDeviceType::KeyboardMouse;
    }

    // 表示用：アクションの割り当てを1件引く（操作説明UIのアイコン選択に使う）
    bool GetPrimaryBinding(uint32_t actionHash, ActiveDeviceType device,
                           InputBinding &outBinding) const override
    {
        const auto it = m_actionBindings.find(actionHash);
        if (it == m_actionBindings.end()) return false;

        for (const InputBinding &b : it->second)
        {
            const bool isPad = (b.deviceType != InputDeviceType::Keyboard && b.deviceType != InputDeviceType::Mouse_Button);
            const bool want  = (device == ActiveDeviceType::GamePad) ? isPad : !isPad;
            if (want)
            {
                outBinding = b;
                return true;
            }
        }
        return false;
    }

    // GUI用：現在登録されている全アクション名を取得
    const std::vector<std::string> &GetActionNames() const { return m_actionNames; }
    const std::vector<std::string> &GetAxisNames() const { return m_axisNames; }

private:
    std::vector<std::string> m_actionNames;
    std::vector<std::string> m_axisNames;

    // --- 内部評価ヘルパー（種別ごとの実処理） ---
    bool EvalActionDown(const std::vector<InputBinding> &binds) const;
    bool EvalActionTriggered(const std::vector<InputBinding> &binds) const;
    bool EvalActionReleased(const std::vector<InputBinding> &binds) const;

public:
    InputFacade(Input *rawInput) : m_rawInput(rawInput) {}
    virtual ~InputFacade() = default;

    // 外部JSONファイルからバインディングを読み込む
    bool LoadConfig(const std::string &filePath) override;

    float GetMouseX() const override
    {
        return static_cast<float>(m_rawInput->GetMouse().GetPositionX());
    }
    float GetMouseY() const override
    {
        return static_cast<float>(m_rawInput->GetMouse().GetPositionY());
    }
    float GetMouseDeltaX() const override
    {
        if (!m_gameInputActive) return 0.0f; // ゲームビュー非フォーカス時は視点を動かさない
        return static_cast<float>(m_rawInput->GetMouse().GetVelocityX());
    }
    float GetMouseDeltaY() const override
    {
        if (!m_gameInputActive) return 0.0f;
        return static_cast<float>(m_rawInput->GetMouse().GetVelocityY());
    }

    // ゲーム側がマウス視点入力を受け付けてよいか（エディタが毎フレーム更新）。
    //   true  = 全画面(F1) もしくは ゲームビューにフォーカスがある
    //   false = 他のエディタ窓を操作中 → マウスデルタを 0 にして視点固定
    //   ※ シェーディング/製品ビルド(非エディタ)では常に true のまま。
    void SetGameInputActive(bool v) { m_gameInputActive = v; }
    bool IsGameInputActive() const override { return m_gameInputActive; }

    // マウスカーソルのロック（生Mouseへ委譲）。マウスルック時に画面中央へ固定する。
    void SetMouseLock(bool lock) override
    {
        if (m_rawInput) m_rawInput->GetMouse().SetCursorLock(lock);
    }
    bool  IsMouseLocked() const override { return m_rawInput && m_rawInput->GetMouse().IsCursorLocked(); }
    float GetScreenWidth() const override
    {
        return static_cast<float>(m_rawInput->GetMouse().GetScreenWidth());
    }
    float GetScreenHeight() const override
    {
        return static_cast<float>(m_rawInput->GetMouse().GetScreenHeight());
    }

    void AddActionBinding(const std::string &actionName, InputDeviceType device, unsigned int keyCode) override;
    void AddAxisBinding(const std::string &axisName, InputDeviceType device, unsigned int keyCode, float scale = 1.0f) override;
    void SetActionTriggerType(const std::string &actionName, TriggerType type) override;

    // エディタ種別で評価する統一窓口
    bool  GetAction(uint32_t actionHash) const override;
    float GetAxis(uint32_t axisHash) const override;

    void SetVibration(float leftMotor, float rightMotor, float duration) override;
};