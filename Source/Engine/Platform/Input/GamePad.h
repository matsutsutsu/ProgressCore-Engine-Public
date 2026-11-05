#pragma once
// ===========================================================================
// 概要: XInput のゲームパッド1台分の入力状態と振動を扱うクラス
//
// 責務:
//   - ボタン（押下中・押した瞬間・離した瞬間）とスティック・トリガーの読み取り
//   - スティックのデッドゾーン処理と -1..1 / 0..1 への正規化
//   - 左スティックを倒した向きを、ボタン（BTN_LSTICK_*）としても扱えるようにする
//   - 振動の開始と、指定時間後の自動停止
// ===========================================================================

using GamePadButton = unsigned int;

class GamePad
{
public:
    static const GamePadButton BTN_UP             = (1 << 0);
    static const GamePadButton BTN_RIGHT          = (1 << 1);
    static const GamePadButton BTN_DOWN           = (1 << 2);
    static const GamePadButton BTN_LEFT           = (1 << 3);
    static const GamePadButton BTN_A              = (1 << 4);
    static const GamePadButton BTN_B              = (1 << 5);
    static const GamePadButton BTN_X              = (1 << 6);
    static const GamePadButton BTN_Y              = (1 << 7);
    static const GamePadButton BTN_START          = (1 << 8);
    static const GamePadButton BTN_BACK           = (1 << 9);
    static const GamePadButton BTN_LEFT_THUMB     = (1 << 10);
    static const GamePadButton BTN_RIGHT_THUMB    = (1 << 11);
    static const GamePadButton BTN_LEFT_SHOULDER  = (1 << 12);
    static const GamePadButton BTN_RIGHT_SHOULDER = (1 << 13);
    static const GamePadButton BTN_LEFT_TRIGGER   = (1 << 14); // トリガーを閾値以上引いたらボタン扱い
    static const GamePadButton BTN_RIGHT_TRIGGER  = (1 << 15);

    // 左スティックを倒した向き。十字キーと同じくボタンとして押した瞬間を取れる（メニューの選択に使う）
    static const GamePadButton BTN_LSTICK_UP    = (1 << 16);
    static const GamePadButton BTN_LSTICK_RIGHT = (1 << 17);
    static const GamePadButton BTN_LSTICK_DOWN  = (1 << 18);
    static const GamePadButton BTN_LSTICK_LEFT  = (1 << 19);

public:
    GamePad() {}
    ~GamePad() {}

    // XInput から状態を読み直し、振動の残り時間を dt（秒）だけ進める
    void Update(float dt);

    // 読み取るコントローラーの番号（0〜3）
    void SetSlot(int slot) { this->slot = slot; }

    // 押されているボタンのビット集合
    GamePadButton GetButton() const { return buttonState[0]; }

    // このフレームに押されたボタンのビット集合
    GamePadButton GetButtonDown() const { return buttonDown; }

    // このフレームに離されたボタンのビット集合
    GamePadButton GetButtonUp() const { return buttonUp; }

    // 押しっぱなし判定
    bool IsDown(GamePadButton button) const
    {
        return (buttonState[0] & button) != 0;
    }

    // 押した瞬間判定
    bool IsTriggered(GamePadButton button) const
    {
        return (buttonDown & button) != 0;
    }

    // 離した瞬間判定
    bool IsReleased(GamePadButton button) const
    {
        return (buttonUp & button) != 0;
    }

    // スティックの傾き（-1..1、デッドゾーン内は 0）
    float GetAxisLX() const { return axisLx; }
    float GetAxisLY() const { return axisLy; }
    float GetAxisRX() const { return axisRx; }
    float GetAxisRY() const { return axisRy; }

    // トリガーの引き量（0..1）
    float GetTriggerL() const { return triggerL; }
    float GetTriggerR() const { return triggerR; }

    // 振動を始める。強さは左（低周波）・右（高周波）とも 0〜1、duration（秒）が過ぎると Update が止める
    // 振動中なら、強さか時間のどれかが今以上のときだけ上書きする
    void SetVibration(float left, float right, float duration);

private:
    GamePadButton buttonState[2] = {0}; // [0] = 今フレーム、[1] = 前フレーム
    GamePadButton buttonDown     = 0;
    GamePadButton buttonUp       = 0;
    float         axisLx         = 0.0f;
    float         axisLy         = 0.0f;
    float         axisRx         = 0.0f;
    float         axisRy         = 0.0f;
    float         triggerL       = 0.0f;
    float         triggerR       = 0.0f;
    int           slot           = 0;

private:
    float m_vibrationTimer = 0.0f; // 振動の残り時間（秒）
    float m_currentLeft    = 0.0f; // 現在の左モーターの強さ
    float m_currentRight   = 0.0f; // 現在の右モーターの強さ

    // XInput へモーターの強さを送る
    void ApplyVibrationHardware(float left, float right);

    // 左スティックの傾きを、向きのボタン（BTN_LSTICK_*）1つに直す。押している向きは戻し切るまで押したままにする
    GamePadButton StickToButtons(GamePadButton prev) const;
};
