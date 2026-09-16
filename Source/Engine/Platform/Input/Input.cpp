#include "Input.h"
#include <cmath> // std::abs 用
#include <tracy/Tracy.hpp>

// 初期化
void Input::Initialize(HWND hWnd)
{
    gamePad  = std::make_unique<GamePad>();
    mouse    = std::make_unique<Mouse>(hWnd);
    keyboard = std::make_unique<Keyboard>();
}

// 更新処理
void Input::Update(float dt)
{
    ZoneScopedN("Input::Update");

    gamePad->Update(dt);
    mouse->Update();
    keyboard->Update();

    // =======================================================
    // 1. アクティブデバイスの自動検知 (Auto-Detection)
    // =======================================================
    // [GamePad の判定]
    // スティックの「ドリフト（指を離しても微小な値が出続ける現象）」によって
    // 意図せずUIが切り替わってチラつくのを防ぐため、必ず 0.1f 以上のデッドゾーンを設ける。
    if (gamePad->GetButtonDown() != 0 ||
        std::abs(gamePad->GetAxisLX()) > 0.1f || std::abs(gamePad->GetAxisLY()) > 0.1f ||
        std::abs(gamePad->GetAxisRX()) > 0.1f || std::abs(gamePad->GetAxisRY()) > 0.1f ||
        gamePad->GetTriggerL() > 0.1f || gamePad->GetTriggerR() > 0.1f)
    {
        m_activeDeviceType = ActiveDeviceType::GamePad;
    }

    // [Mouse の判定]
    // マウスのクリック、または移動（微小な振動を弾くため 0.1f を閾値とする）を検知
    if (mouse->GetButtonDown() != 0 ||
        std::abs(mouse->GetVelocityX()) > 0.1f || std::abs(mouse->GetVelocityY()) > 0.1f)
    {
        m_activeDeviceType = ActiveDeviceType::KeyboardMouse;
    }

    // 2. Logic用の入力バッファに「蓄積」する
    //    |= (OR演算) を使うことで、Clearされるまでの間に一度でも押されれば
    //    フラグが立ったままになります。
    m_logicPadButtonDown |= gamePad->GetButtonDown();
    m_logicMouseButtonDown |= mouse->GetButtonDown();

    // キーボードのトリガー蓄積 & アクティブデバイス判定
    for (int i = 0; i < 256; i++)
    {
        bool triggered = keyboard->IsTriggered(i);
        m_logicKeyTriggered[i] |= triggered;

        // [Keyboard の判定]
        // 何かキーが押されたら、強制的にキーボード＆マウス操作とみなす
        if (triggered)
        {
            m_activeDeviceType = ActiveDeviceType::KeyboardMouse;
        }
    }
}

// --- リセット処理 ---
// ※ FrameWork.cpp の FixedUpdate が終わった後に呼ばれる
void Input::ClearLogicInput()
{
    m_logicPadButtonDown   = 0;
    m_logicMouseButtonDown = 0;
    std::memset(m_logicKeyTriggered, 0, sizeof(m_logicKeyTriggered));
}