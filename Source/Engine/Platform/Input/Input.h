#pragma once

#include <memory>
#include "GamePad.h"
#include "Mouse.h"
#include "Keyboard.h"

#include "IInputAPI.h" // ActiveDeviceType を使うためにインクルード

// インプット
class Input
{
private:
	Input() = default;
	~Input() = default;

public:
	// インスタンス取得
	static Input& Instance()
	{
		static Input instance;
		return instance;
	}

	// 初期化
	void Initialize(HWND hWnd);

	// 更新処理
	void Update(float dt);

	// ゲームパッド取得
	GamePad& GetGamePad() { return *gamePad; }

	// マウス取得
	Mouse& GetMouse() { return *mouse; }

	// キーボード取得 (追加)
    Keyboard &GetKeyboard() { return *keyboard; }

	// --- Logic更新用（固定フレーム用）の入力取得関数 ---
	// 瞬間的な入力（GetButtonDown）を取りこぼさないために、
	// ClearLogicInput() が呼ばれるまでフラグを保持し続けます。
	unsigned int GetPadButtonDownLogic() const { return _logicPadButtonDown; }
	unsigned int GetMouseButtonDownLogic() const { return _logicMouseButtonDown; }

	// 指定キーボードのトリガー蓄積確認
	bool GetKeyboardLogicTriggered(int key) const {
		if (key < 0 || key >= 256) return false;
		return _logicKeyTriggered[key];
	}

	// Logic更新が終わったら呼ぶリセット関数
	void ClearLogicInput();

	// 直近で操作されたデバイスを取得
	ActiveDeviceType GetActiveDeviceType() const { return _activeDeviceType; }

	bool GetLogicKeyTriggered(int keyCode) const { return _logicKeyTriggered[keyCode]; }
	unsigned int GetLogicMouseButtonDown() const { return _logicMouseButtonDown; }
	unsigned int GetLogicPadButtonDown() const { return _logicPadButtonDown; }
private:
	std::unique_ptr<GamePad>	gamePad;
	std::unique_ptr<Mouse>		mouse;
    std::unique_ptr<Keyboard>   keyboard;

	// --- アクティブデバイス保持用 ---
	ActiveDeviceType _activeDeviceType = ActiveDeviceType::KeyboardMouse;

	// --- 入力蓄積用変数 ---
	unsigned int _logicPadButtonDown = 0;
	unsigned int _logicMouseButtonDown = 0;
	bool _logicKeyTriggered[256] = { false }; // キーボード用蓄積バッファ
};
