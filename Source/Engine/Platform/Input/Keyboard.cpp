#include "Keyboard.h"

void Keyboard::Update()
{
    // 現在の状態を過去の状態として保存
    memcpy(oldState, currentState, sizeof(currentState));

    // 全キーの状態を取得
    // GetKeyboardState はメッセージポンプに依存するため、
    // リアルタイム性の高いゲームでは GetAsyncKeyState をループさせるのが確実です。
    for (int i = 0; i < 256; i++) {
        // 最上位ビットが立っていれば押されている
        if (GetAsyncKeyState(i) & 0x8000) {
            currentState[i] = 1;
        }
        else {
            currentState[i] = 0;
        }
    }
}

bool Keyboard::IsDown(int key) const
{
    // 範囲チェック推奨ですが、速度優先で省略する場合もあります
    if (key < 0 || key >= 256) return false;
    return currentState[key] != 0;
}

bool Keyboard::IsTriggered(int key) const
{
    if (key < 0 || key >= 256) return false;
    // 今押されていて、かつ 前は押されていなかった
    return (currentState[key] != 0) && (oldState[key] == 0);
}

bool Keyboard::IsReleased(int key) const
{
    if (key < 0 || key >= 256) return false;
    // 今押されていなくて、かつ 前は押されていた
    return (currentState[key] == 0) && (oldState[key] != 0);
}