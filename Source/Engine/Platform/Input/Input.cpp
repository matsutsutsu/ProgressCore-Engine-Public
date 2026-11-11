#include "Input.h"
#include <cmath>
#include <cstring>
#include <tracy/Tracy.hpp>

void Input::Initialize(HWND hWnd)
{
    gamePad  = std::make_unique<GamePad>();
    mouse    = std::make_unique<Mouse>(hWnd);
    keyboard = std::make_unique<Keyboard>();
}



void Input::Update(float dt)
{
    ZoneScopedN("Input::Update");

    gamePad->Update(dt);
    mouse->Update();
    keyboard->Update();

    // 裏に回っている間は、キーとマウスを何も押していない扱いにする
    //   Alt+Tab で離れると「離した」の通知が届かず、戻ったときに押しっぱなしのまま歩き続ける
    //   マウスは画面全体のボタンを読むので、そのままだと他のアプリでのクリックを拾う
    if (!m_appActive)
    {
        keyboard->Clear();
        mouse->ClearButtons();
    }

    // 1. 直近に操作されたデバイスの判定
    // ゲームパッド: スティックのドリフト（指を離しても微小な値が出続ける現象）で
    // UI のアイコン表示が勝手に切り替わらないよう、0.1 未満の傾きは無視する
    if (gamePad->GetButtonDown() != 0 ||
        std::abs(gamePad->GetAxisLX()) > 0.1f || std::abs(gamePad->GetAxisLY()) > 0.1f ||
        std::abs(gamePad->GetAxisRX()) > 0.1f || std::abs(gamePad->GetAxisRY()) > 0.1f ||
        gamePad->GetTriggerL() > 0.1f || gamePad->GetTriggerR() > 0.1f)
    {
        m_activeDeviceType = ActiveDeviceType::GamePad;
    }

    // マウス: クリックか移動（1ピクセル以上）でキーボード＆マウスに戻す
    if (mouse->GetButtonDown() != 0 ||
        std::abs(mouse->GetVelocityX()) > 0.1f || std::abs(mouse->GetVelocityY()) > 0.1f)
    {
        m_activeDeviceType = ActiveDeviceType::KeyboardMouse;
    }

    // 2. ロジック用に「押した瞬間」を溜める
    //    OR で溜めるので、ClearLogicInput までに一度でも押されればフラグが立ったままになる
    m_logicPadButtonDown |= gamePad->GetButtonDown();
    m_logicMouseButtonDown |= mouse->GetButtonDown();

    // キーボードの「押した瞬間」を溜め、何か押されたらキーボード＆マウスとみなす
    for (int i = 0; i < 256; i++)
    {
        bool triggered = keyboard->IsTriggered(i);
        m_logicKeyTriggered[i] |= triggered;

        if (triggered)
        {
            m_activeDeviceType = ActiveDeviceType::KeyboardMouse;
        }
    }
}



void Input::ClearLogicInput()
{
    m_logicPadButtonDown   = 0;
    m_logicMouseButtonDown = 0;
    std::memset(m_logicKeyTriggered, 0, sizeof(m_logicKeyTriggered));
}