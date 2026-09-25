#pragma once
// ===========================================================================
// 概要: マウスのボタン・ホイール・カーソル位置を毎フレーム取得するクラス。
//
// 責務:
//   - ボタン（押下中・押した瞬間・離した瞬間）とホイール量の管理
//   - カーソル位置と前フレームからの移動量
//   - マウスルック用のカーソルロック（画面中央へ戻し、OS カーソルを隠す）
// ===========================================================================

#include <Windows.h>
#include <DirectXMath.h>

using MouseButton = unsigned int;

class Mouse
{
public:
    static const MouseButton BTN_LEFT   = (1 << 0);
    static const MouseButton BTN_MIDDLE = (1 << 1);
    static const MouseButton BTN_RIGHT  = (1 << 2);

public:
    Mouse(HWND hWnd);
    ~Mouse() {}

    void Update();

    // 押されているボタンのビット集合
    MouseButton GetButton() const { return buttonState[0]; }

    // このフレームに押されたボタンのビット集合
    MouseButton GetButtonDown() const { return buttonDown; }

    // このフレームに離されたボタンのビット集合
    MouseButton GetButtonUp() const { return buttonUp; }

    // 押しっぱなし判定
    bool IsDown(MouseButton button) const
    {
        return (buttonState[0] & button) != 0;
    }

    // 押した瞬間判定
    bool IsTriggered(MouseButton button) const
    {
        return (buttonDown & button) != 0;
    }

    // 離した瞬間判定
    bool IsReleased(MouseButton button) const
    {
        return (buttonUp & button) != 0;
    }

    // ホイール量を加算する。WM_MOUSEWHEEL を受けた Framework が呼ぶ
    void SetWheel(int wheel) { this->wheel[0] += wheel; }

    // 前フレームの間に回されたホイール量（正 = 上方向）
    int GetWheel() const { return wheel[1]; }

    // カーソル座標
    DirectX::XMFLOAT2 GetPosition() const
    {
        return DirectX::XMFLOAT2(static_cast<float>(positionX[0]), static_cast<float>(positionY[0]));
    }

    int GetPositionX() const { return positionX[0]; }
    int GetPositionY() const { return positionY[0]; }

    // 前フレームのカーソル座標
    int GetOldPositionX() const { return positionX[1]; }
    int GetOldPositionY() const { return positionY[1]; }

    // 前フレームからの移動量（ピクセル）
    int GetVelocityX() const { return positionX[0] - positionX[1]; }
    int GetVelocityY() const { return positionY[0] - positionY[1]; }

    // 起動時のクライアント領域のサイズ
    int GetScreenWidth() const { return screenWidth; }
    int GetScreenHeight() const { return screenHeight; }

    // カーソルロック（マウスルック用）。
    //   true にすると Update() が毎フレーム、カーソルを画面中央へ戻して OS カーソルを隠す。
    //   移動量(GetVelocity)は「中央からのずれ」になる。
    //   ウィンドウが非フォーカスの間はロックしない（alt-tab でカーソルが閉じ込められない）。
    void SetCursorLock(bool lock) { m_lockRequested = lock; }
    bool IsCursorLocked() const { return m_cursorHidden; }

private:
    MouseButton buttonState[2] = {0}; // [0] = 今フレーム、[1] = 前フレーム
    MouseButton buttonDown     = 0;
    MouseButton buttonUp       = 0;
    int         positionX[2]   = {0}; // [0] = 今フレーム、[1] = 前フレーム
    int         positionY[2]   = {0};
    int         wheel[2]       = {0}; // [0] = 次の Update までに溜める量、[1] = 確定した量
    int         screenWidth    = 0;
    int         screenHeight   = 0;
    HWND        hWnd           = nullptr;

    bool m_lockRequested = false; // ゲーム側からのロック要求
    bool m_cursorHidden  = false; // 実際にロック中か（ShowCursor の表示カウンタの整合とロック開始フレームの検出に使う）
};
