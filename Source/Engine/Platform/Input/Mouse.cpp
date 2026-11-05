#include "Mouse.h"
#include <tracy/Tracy.hpp>

// 並び順が Mouse::BTN_* のビット位置に対応する
static const int KeyMap[] =
    {
        VK_LBUTTON, // BTN_LEFT
        VK_MBUTTON, // BTN_MIDDLE
        VK_RBUTTON, // BTN_RIGHT
};

Mouse::Mouse(HWND hWnd)
    : hWnd(hWnd)
{
    RECT rc;
    GetClientRect(hWnd, &rc);
    screenWidth  = rc.right - rc.left;
    screenHeight = rc.bottom - rc.top;
}



void Mouse::Update()
{
    ZoneScopedN("Mouse::Update");

    MouseButton newButtonState = 0;
    for (int i = 0; i < ARRAYSIZE(KeyMap); ++i)
    {
        if (::GetAsyncKeyState(KeyMap[i]) & 0x8000)
        {
            newButtonState |= (1 << i);
        }
    }
    // ホイール: 前フレームまでに溜めた量を今フレームの値として確定する
    wheel[1] = wheel[0];
    wheel[0] = 0;

    buttonState[1] = buttonState[0];
    buttonState[0] = newButtonState;
    buttonDown     = ~buttonState[1] & newButtonState; // 押した瞬間
    buttonUp       = ~newButtonState & buttonState[1]; // 離した瞬間

    POINT cursor;
    ::GetCursorPos(&cursor);
    ::ScreenToClient(hWnd, &cursor);
    RECT rc;
    GetClientRect(hWnd, &rc);
    UINT screenW   = rc.right - rc.left;
    UINT screenH   = rc.bottom - rc.top;
    UINT viewportW = screenWidth;
    UINT viewportH = screenHeight;
    // クライアント座標を、起動時のクライアントサイズ（= バックバッファの大きさ）の座標に直す
    // ウィンドウの大きさが起動時と同じなら何も変わらない
    positionX[1] = positionX[0];
    positionY[1] = positionY[0];
    positionX[0] = (LONG)(cursor.x / static_cast<float>(screenW) * static_cast<float>(viewportW));
    positionY[0] = (LONG)(cursor.y / static_cast<float>(screenH) * static_cast<float>(viewportH));

    // カーソルロック（マウスルック）：カーソルを画面中央へ戻し、画面端で止まらないようにする
    //   ・ウィンドウが非フォーカスならロックしない（alt-tab でカーソルが閉じ込められない）
    //   ・ロック開始フレームは視点が飛ばないよう移動量を 0 にする
    bool focused       = (::GetForegroundWindow() == hWnd);
    bool effectiveLock = m_lockRequested && focused && screenW > 0 && screenH > 0;

    if (effectiveLock)
    {
        // クライアント中央を positionX と同じ換算（scaled座標系）で求める
        int cxClient      = (int)screenW / 2;
        int cyClient      = (int)screenH / 2;
        int scaledCenterX = (viewportW > 0) ? (int)(cxClient / static_cast<float>(viewportW) * static_cast<float>(screenW)) : cxClient;
        int scaledCenterY = (viewportH > 0) ? (int)(cyClient / static_cast<float>(viewportH) * static_cast<float>(screenH)) : cyClient;

        if (!m_cursorHidden)
        {
            // ロック開始フレーム：移動量0（視点ジャンプ防止）＋カーソルを隠す
            positionX[1] = positionX[0];
            positionY[1] = positionY[0];
            ::ShowCursor(FALSE);
            m_cursorHidden = true;
        }
        else
        {
            // 「中央からのズレ」を今フレームの移動量として供給する
            positionX[1] = scaledCenterX;
            positionY[1] = scaledCenterY;
        }

        // 物理カーソルをクライアント中央へ戻す
        POINT c = {cxClient, cyClient};
        ::ClientToScreen(hWnd, &c);
        ::SetCursorPos(c.x, c.y);
    }
    else if (m_cursorHidden)
    {
        // ロック解除：カーソルを再表示する
        ::ShowCursor(TRUE);
        m_cursorHidden = false;
    }
}