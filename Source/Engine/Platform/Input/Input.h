#pragma once
// ===========================================================================
// 概要: ゲームパッド・マウス・キーボードをまとめて保持し、毎フレーム更新するシングルトン。
//
// 責務:
//   - 3デバイスの生成と毎フレームの Update
//   - 「押した瞬間」の入力を固定ステップのロジック更新まで溜めておく
//   - 直近に操作されたデバイス（キーボード＆マウス／ゲームパッド）の判定
//
// 備考: ゲーム側は IInputAPI（InputFacade）経由で読む。ここを直接使うのは
//       Framework・BaseScene・InputFacade と、デバッグ用のフリーカメラだけ。
// ===========================================================================

#include <memory>
#include "GamePad.h"
#include "Mouse.h"
#include "Keyboard.h"

#include "IInputAPI.h" // ActiveDeviceType

class Input
{
private:
    Input()  = default;
    ~Input() = default;

public:
    static Input &Instance()
    {
        static Input instance;
        return instance;
    }

    // 各デバイスを生成する。hWnd はマウス座標の基準にするウィンドウ
    void Initialize(HWND hWnd);

    // 描画フレームごとに呼ぶ。デバイスを更新し、「押した瞬間」を溜める
    void Update(float dt);

    GamePad  &GetGamePad() { return *gamePad; }
    Mouse    &GetMouse() { return *mouse; }
    Keyboard &GetKeyboard() { return *keyboard; }

    // --- ロジック（固定ステップ）用の「押した瞬間」 ---
    /**
     * @brief 固定ステップ用の「押した瞬間」を返す。
     *
     * 描画フレームと固定ステップの回数は一致しないので、1フレームだけの「押した瞬間」をそのまま読むと、ステップが走らなかったフレームの入力を取りこぼす。
     * ClearLogicInput() が呼ばれるまで OR で溜めておく。
     *
     * @param keyCode 調べるキー。
     * @return 前回の ClearLogicInput() 以降に押されていれば true。
     */
    bool GetLogicKeyTriggered(int keyCode) const
    {
        if (keyCode < 0 || keyCode >= 256) return false;
        return m_logicKeyTriggered[keyCode];
    }
    unsigned int GetLogicMouseButtonDown() const { return m_logicMouseButtonDown; }
    unsigned int GetLogicPadButtonDown() const { return m_logicPadButtonDown; }

    // 固定ステップの処理が終わったら呼び、溜めた「押した瞬間」を下ろす
    void ClearLogicInput();

    // 直近で操作されたデバイス
    ActiveDeviceType GetActiveDeviceType() const { return m_activeDeviceType; }

    // アプリが前面にあるか。Framework が WM_ACTIVATEAPP を受けて書く
    void SetAppActive(bool active) { m_appActive = active; }
    bool IsAppActive() const { return m_appActive; }

private:
    std::unique_ptr<GamePad>  gamePad;
    std::unique_ptr<Mouse>    mouse;
    std::unique_ptr<Keyboard> keyboard;

    ActiveDeviceType m_activeDeviceType = ActiveDeviceType::KeyboardMouse;
    bool             m_appActive        = true; // アプリが前面にあるか

    // ClearLogicInput までに溜めた「押した瞬間」
    unsigned int m_logicPadButtonDown     = 0;
    unsigned int m_logicMouseButtonDown   = 0;
    bool         m_logicKeyTriggered[256] = {false};
};
