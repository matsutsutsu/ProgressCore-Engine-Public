#pragma once
// ===========================================================================
// 概要: IInputAPI の実装。InputConfig.json の割り当てに従って、Input の生の入力を
//       アクション／軸として評価する。
//
// 責務:
//   - InputConfig.json の読み込み（ホットリロード対応）
//   - アクションを発火タイミング（Down / Triggered / Released）に従って評価する
//   - 軸を複数の割り当ての合計（-1..1 にクランプ）として評価する
//   - エディタでウィジェットを操作中・ゲームビュー非フォーカス中はゲーム入力を止める
// ===========================================================================
#include "IInputAPI.h"
#include "Input.h"
#include <unordered_map>
#include <string>

class InputFacade : public IInputAPI
{
private:
    Input *m_rawInput; // 生のデバイス入力（所有しない。実体は Input のシングルトン）

    // 名前のハッシュ -> 割り当ての一覧
    std::unordered_map<uint32_t, std::vector<InputBinding>> m_actionBindings;
    std::unordered_map<uint32_t, std::vector<InputBinding>> m_axisBindings;

    // アクションごとの発火種別（未登録は Down 扱い）
    std::unordered_map<uint32_t, TriggerType> m_actionTriggerTypes;

    // ゲームビューにフォーカスがある／全画面のときだけマウス視点を通す（既定 true）。
    bool m_gameInputActive = true;

public:
    ActiveDeviceType GetActiveDeviceType() const override
    {
        return m_rawInput ? m_rawInput->GetActiveDeviceType() : ActiveDeviceType::KeyboardMouse;
    }

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

private:
    // 発火タイミングごとの評価。いずれかの割り当てが条件を満たせば true
    bool EvalActionDown(const std::vector<InputBinding> &binds) const;
    bool EvalActionTriggered(const std::vector<InputBinding> &binds) const;
    bool EvalActionReleased(const std::vector<InputBinding> &binds) const;

public:
    InputFacade(Input *rawInput) : m_rawInput(rawInput) {}
    virtual ~InputFacade() = default;

    // JSON から割り当てを読み込む。既存の割り当てはすべて捨てて読み直す
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

    // ゲーム側がマウス視点入力を受け付けてよいか（EditorWindowManager が毎フレーム更新する）。
    //   true  = 全画面(F1) もしくはゲームビューにフォーカスがある
    //   false = 他のエディタ窓を操作中 → マウスの移動量を 0 にして視点を止める
    //   エディタ無しの構成では誰も呼ばないので true のまま。
    void SetGameInputActive(bool v) { m_gameInputActive = v; }
    bool IsGameInputActive() const override { return m_gameInputActive; }

    // マウスカーソルのロック（Mouse へ委譲）
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

    bool  GetAction(uint32_t actionHash) const override;
    float GetAxis(uint32_t axisHash) const override;

    void SetVibration(float leftMotor, float rightMotor, float duration) override;
};