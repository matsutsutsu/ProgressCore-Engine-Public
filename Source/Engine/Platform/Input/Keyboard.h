#pragma once
// ===========================================================================
// 概要: キーボード全キー（仮想キーコード 0〜255）の押下状態を毎フレーム取得するクラス
//
// 責務:
//   - GetKeyboardState で全キーを読み、前フレームの状態を残す
//   - 押しっぱなし・押した瞬間・離した瞬間の判定
// ===========================================================================
#include <Windows.h>

#include <algorithm>
#include <iterator>

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

    // すべてのキーを「押されていない」にする（アプリが裏に回り、離した通知が届かないときに使う）
    void Clear()
    {
        std::fill(std::begin(currentState), std::end(currentState), BYTE{0});
        std::fill(std::begin(oldState), std::end(oldState), BYTE{0});
    }

private:
    BYTE currentState[256] = {0}; // 今フレームのキー状態（1 = 押されている）
    BYTE oldState[256]     = {0}; // 前フレームのキー状態
};