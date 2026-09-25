#pragma once
// ===========================================================================
// 概要: キーボード全キー（仮想キーコード 0〜255）の押下状態を毎フレーム取得するクラス。
//
// 責務:
//   - GetKeyboardState で全キーを読み、前フレームの状態を残す
//   - 押しっぱなし・押した瞬間・離した瞬間の判定
// ===========================================================================
#include <Windows.h>

class Keyboard
{
public:
    Keyboard()  = default;
    ~Keyboard() = default;

    void Update();

    // キーが押されているか（押しっぱなし）。key は仮想キーコード
    bool IsDown(int key) const;

    // キーが押された瞬間か
    bool IsTriggered(int key) const;

    // キーが離された瞬間か
    bool IsReleased(int key) const;

private:
    BYTE currentState[256] = {0}; // 今フレームのキー状態（1 = 押されている）
    BYTE oldState[256]     = {0}; // 前フレームのキー状態
};