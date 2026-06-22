#pragma once
// =====================================================================
// InputKeyTable.h
// ---------------------------------------------------------------------
// 入力キー定義の「単一の真実の源 (Single Source of Truth)」。
//
// ・InputEditorWindow のコンボボックス選択肢
// ・InputFacade::StringToKeyCode の文字列 -> キーコード変換
//
// この2つは必ず同じ定義から生成すること。
// 片方にだけキーを追加して「保存できるのに反応しない」という
// 不整合バグを構造的に防ぐためのテーブル。
//
// キーを増やしたい場合は、このファイルの該当テーブルに1行足すだけでよい。
// =====================================================================

#include <Windows.h>
#include <Xinput.h>
#include <string_view>
#include <string>
#include <array>
#include <vector>
#include "GamePad.h"
#include "Mouse.h"

namespace InputKeyTable
{
// 1エントリ = 「JSONに書かれる文字列」と「実際のキーコード」のペア
struct KeyDef
{
    std::string_view label; // 表示名 / JSON保存文字列 (例 "SHIFT")
    unsigned int     code;  // 実際のコード (VK_SHIFT や GamePad::BTN_A 等)
};

// ---------------------------------------------------------------
// キーボード
// ---------------------------------------------------------------
inline constexpr std::array<KeyDef, 96> Keyboard = {{
    {"NONE", 0},

    // --- 修飾キー (今まで設定できなかったもの) ---
    {"SHIFT", VK_SHIFT},
    {"L_SHIFT", VK_LSHIFT},
    {"R_SHIFT", VK_RSHIFT},
    {"CTRL", VK_CONTROL},
    {"L_CTRL", VK_LCONTROL},
    {"R_CTRL", VK_RCONTROL},
    {"ALT", VK_MENU},
    {"L_ALT", VK_LMENU},
    {"R_ALT", VK_RMENU},

    // --- 制御 / 編集キー ---
    {"SPACE", VK_SPACE},
    {"ENTER", VK_RETURN},
    {"ESC", VK_ESCAPE},
    {"TAB", VK_TAB},
    {"BACK", VK_BACK},
    {"DELETE", VK_DELETE},
    {"INSERT", VK_INSERT},
    {"HOME", VK_HOME},
    {"END", VK_END},
    {"PAGE_UP", VK_PRIOR},
    {"PAGE_DOWN", VK_NEXT},
    {"CAPS_LOCK", VK_CAPITAL},

    // --- 矢印 ---
    {"UP", VK_UP},
    {"DOWN", VK_DOWN},
    {"LEFT", VK_LEFT},
    {"RIGHT", VK_RIGHT},

    // --- アルファベット (VKコード = ASCII) ---
    {"A", 'A'},
    {"B", 'B'},
    {"C", 'C'},
    {"D", 'D'},
    {"E", 'E'},
    {"F", 'F'},
    {"G", 'G'},
    {"H", 'H'},
    {"I", 'I'},
    {"J", 'J'},
    {"K", 'K'},
    {"L", 'L'},
    {"M", 'M'},
    {"N", 'N'},
    {"O", 'O'},
    {"P", 'P'},
    {"Q", 'Q'},
    {"R", 'R'},
    {"S", 'S'},
    {"T", 'T'},
    {"U", 'U'},
    {"V", 'V'},
    {"W", 'W'},
    {"X", 'X'},
    {"Y", 'Y'},
    {"Z", 'Z'},

    // --- 数字 (メインキー上段) ---
    {"0", '0'},
    {"1", '1'},
    {"2", '2'},
    {"3", '3'},
    {"4", '4'},
    {"5", '5'},
    {"6", '6'},
    {"7", '7'},
    {"8", '8'},
    {"9", '9'},

    // --- テンキー ---
    {"NUM_0", VK_NUMPAD0},
    {"NUM_1", VK_NUMPAD1},
    {"NUM_2", VK_NUMPAD2},
    {"NUM_3", VK_NUMPAD3},
    {"NUM_4", VK_NUMPAD4},
    {"NUM_5", VK_NUMPAD5},
    {"NUM_6", VK_NUMPAD6},
    {"NUM_7", VK_NUMPAD7},
    {"NUM_8", VK_NUMPAD8},
    {"NUM_9", VK_NUMPAD9},

    // --- ファンクションキー ---
    {"F1", VK_F1},
    {"F2", VK_F2},
    {"F3", VK_F3},
    {"F4", VK_F4},
    {"F5", VK_F5},
    {"F6", VK_F6},
    {"F7", VK_F7},
    {"F8", VK_F8},
    {"F9", VK_F9},
    {"F10", VK_F10},
    {"F11", VK_F11},
    {"F12", VK_F12},
}};

// ---------------------------------------------------------------
// マウスボタン (Mouse:: のビットフラグ)
// ---------------------------------------------------------------
inline constexpr std::array<KeyDef, 4> MouseButtons = {{
    {"NONE", 0},
    {"BTN_LEFT", Mouse::BTN_LEFT},
    {"BTN_RIGHT", Mouse::BTN_RIGHT},
    {"BTN_MIDDLE", Mouse::BTN_MIDDLE},
}};

// ---------------------------------------------------------------
// ゲームパッドボタン (GamePad:: のビットフラグ)
// ---------------------------------------------------------------
inline constexpr std::array<KeyDef, 20> GamePadButtons = {{
    {"NONE", 0},
    {"BTN_A", GamePad::BTN_A},
    {"BTN_B", GamePad::BTN_B},
    {"BTN_X", GamePad::BTN_X},
    {"BTN_Y", GamePad::BTN_Y},
    {"BTN_DPAD_UP", GamePad::BTN_UP},
    {"BTN_DPAD_DOWN", GamePad::BTN_DOWN},
    {"BTN_DPAD_LEFT", GamePad::BTN_LEFT},
    {"BTN_DPAD_RIGHT", GamePad::BTN_RIGHT},
    {"BTN_LEFT_SHOULDER", GamePad::BTN_LEFT_SHOULDER},
    {"BTN_RIGHT_SHOULDER", GamePad::BTN_RIGHT_SHOULDER},
    {"BTN_LEFT_TRIGGER", GamePad::BTN_LEFT_TRIGGER},
    {"BTN_RIGHT_TRIGGER", GamePad::BTN_RIGHT_TRIGGER},
    {"BTN_LEFT_THUMB", GamePad::BTN_LEFT_THUMB},
    {"BTN_RIGHT_THUMB", GamePad::BTN_RIGHT_THUMB},
    {"BTN_START", GamePad::BTN_START},
    {"BTN_BACK", GamePad::BTN_BACK},
    // 軸系では使われないがボタン割り当て用に予約
    {"BTN_SPACE", GamePad::BTN_SPACE},
    {"BTN_Q", GamePad::BTN_Q},
    {"BTN_E", GamePad::BTN_E},
}};

// =================================================================
// 統合エントリ
// -----------------------------------------------------------------
// エディタの「キーを直接選ぶ」UI用。
// Device文字列(JSON保存される) と Key文字列(JSON保存される) と
// 人が読む表示名(PS表記など) をひとまとめにしている。
//
// ここに1行足せば、エディタの選択肢に自動で増える。
// =================================================================
struct UnifiedEntry
{
    std::string_view device;  // JSON "Device" (例 "GamePad_Button")
    std::string_view key;     // JSON "Key"    (例 "BTN_B")
    std::string_view display; // 表示名        (例 "B (×ボタン)")
};

// ----- キーボード -----
inline const std::vector<UnifiedEntry> &KeyboardEntries()
{
    static const std::vector<UnifiedEntry> v = [] {
        std::vector<UnifiedEntry> out;
        for (const auto &d : Keyboard)
        {
            if (d.label == "NONE")
                continue;
            out.push_back({"Keyboard", d.label, d.label});
        }
        return out;
    }();
    return v;
}

// ----- マウス -----
inline const std::vector<UnifiedEntry> &MouseEntries()
{
    static const std::vector<UnifiedEntry> v = {
        {"Mouse_Button", "BTN_LEFT", "Mouse Left"},
        {"Mouse_Button", "BTN_RIGHT", "Mouse Right"},
        {"Mouse_Button", "BTN_MIDDLE", "Mouse Middle"},
    };
    return v;
}

// ----- ゲームパッド（ボタンは直接ここに並ぶ）-----
//   display にXbox/PS両方の呼び名を併記して分かりやすく。
inline const std::vector<UnifiedEntry> &GamePadEntries()
{
    static const std::vector<UnifiedEntry> v = {
        // フェイスボタン
        {"GamePad_Button", "BTN_A", "A  (×ボタン / 下)"},
        {"GamePad_Button", "BTN_B", "B  (○ボタン / 右)"},
        {"GamePad_Button", "BTN_X", "X  (□ボタン / 左)"},
        {"GamePad_Button", "BTN_Y", "Y  (△ボタン / 上)"},
        // 十字キー
        {"GamePad_Button", "BTN_DPAD_UP", "D-Pad Up"},
        {"GamePad_Button", "BTN_DPAD_DOWN", "D-Pad Down"},
        {"GamePad_Button", "BTN_DPAD_LEFT", "D-Pad Left"},
        {"GamePad_Button", "BTN_DPAD_RIGHT", "D-Pad Right"},
        // ショルダー / トリガー（ボタン扱い）
        {"GamePad_Button", "BTN_LEFT_SHOULDER", "LB / L1"},
        {"GamePad_Button", "BTN_RIGHT_SHOULDER", "RB / R1"},
        {"GamePad_Button", "BTN_LEFT_TRIGGER", "LT / L2 (button)"},
        {"GamePad_Button", "BTN_RIGHT_TRIGGER", "RT / R2 (button)"},
        // スティック押し込み
        {"GamePad_Button", "BTN_LEFT_THUMB", "L-Stick Press (L3)"},
        {"GamePad_Button", "BTN_RIGHT_THUMB", "R-Stick Press (R3)"},
        // システム
        {"GamePad_Button", "BTN_START", "Start / Menu"},
        {"GamePad_Button", "BTN_BACK", "Back / View"},
    };
    return v;
}

// ----- ゲームパッド（軸：Axisタブ専用）-----
inline const std::vector<UnifiedEntry> &GamePadAxisEntries()
{
    static const std::vector<UnifiedEntry> v = {
        {"GamePad_AxisLX", "NONE", "L-Stick X"},
        {"GamePad_AxisLY", "NONE", "L-Stick Y"},
        {"GamePad_AxisRX", "NONE", "R-Stick X"},
        {"GamePad_AxisRY", "NONE", "R-Stick Y"},
        {"GamePad_TriggerL", "NONE", "L Trigger (analog)"},
        {"GamePad_TriggerR", "NONE", "R Trigger (analog)"},
    };
    return v;
}

// 与えられた (device,key) に一致する統合エントリの表示名を返す。
// 見つからなければ "device / key" を組み立てて返す。
inline std::string DisplayNameOf(const std::string &device, const std::string &key)
{
    auto search = [&](const std::vector<UnifiedEntry> &list) -> const UnifiedEntry * {
        for (const auto &e : list)
            if (e.device == device && e.key == key)
                return &e;
        return nullptr;
    };
    if (auto *e = search(KeyboardEntries()))
        return std::string(e->display);
    if (auto *e = search(MouseEntries()))
        return std::string(e->display);
    if (auto *e = search(GamePadEntries()))
        return std::string(e->display);
    if (auto *e = search(GamePadAxisEntries()))
        return std::string(e->display);
    return device + " / " + key;
}

// ---------------------------------------------------------------
// キャプチャ用: 現在押されているゲームパッドのボタンを検出。
//   見つかった (device,key) を返す。なければ false。
//   ※ XInputState は呼び出し側で取得済みのものを渡す。
// ---------------------------------------------------------------
struct PadHit
{
    std::string_view device;
    std::string_view key;
};

inline bool DetectPressedGamePadButton(unsigned short wButtons, PadHit &out)
{
    struct Map
    {
        unsigned short   bit;
        std::string_view key;
    };
    static const Map maps[] = {
        {0x1000 /*A*/, "BTN_A"},
        {0x2000 /*B*/, "BTN_B"},
        {0x4000 /*X*/, "BTN_X"},
        {0x8000 /*Y*/, "BTN_Y"},
        {0x0001 /*DPAD_UP*/, "BTN_DPAD_UP"},
        {0x0002 /*DPAD_DOWN*/, "BTN_DPAD_DOWN"},
        {0x0004 /*DPAD_LEFT*/, "BTN_DPAD_LEFT"},
        {0x0008 /*DPAD_RIGHT*/, "BTN_DPAD_RIGHT"},
        {0x0100 /*LEFT_SHOULDER*/, "BTN_LEFT_SHOULDER"},
        {0x0200 /*RIGHT_SHOULDER*/, "BTN_RIGHT_SHOULDER"},
        {0x0040 /*LEFT_THUMB*/, "BTN_LEFT_THUMB"},
        {0x0080 /*RIGHT_THUMB*/, "BTN_RIGHT_THUMB"},
        {0x0010 /*START*/, "BTN_START"},
        {0x0020 /*BACK*/, "BTN_BACK"},
    };
    for (const auto &m : maps)
    {
        if (wButtons & m.bit)
        {
            out = {"GamePad_Button", m.key};
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------
// 軸・トリガーは具体的なキー指定が不要なデバイス
// ---------------------------------------------------------------
inline bool IsAnalogDevice(const std::string &device)
{
    return device.rfind("GamePad_Axis", 0) == 0 || device.rfind("GamePad_Trigger", 0) == 0;
}

// デバイス大分類（UIのデバイス選択用）
enum class DeviceClass
{
    Keyboard,
    Mouse,
    GamePad
};

inline DeviceClass ClassOf(const std::string &device)
{
    if (device == "Mouse_Button")
        return DeviceClass::Mouse;
    if (device.rfind("GamePad", 0) == 0)
        return DeviceClass::GamePad;
    return DeviceClass::Keyboard;
}

// ---------------------------------------------------------------
// 文字列 -> キーコード
//   InputFacade から呼ぶ。全テーブルを横断検索する。
// ---------------------------------------------------------------
inline unsigned int StringToKeyCode(std::string_view str)
{
    if (str.empty() || str == "NONE")
        return 0;
    for (const auto &d : Keyboard)
        if (d.label == str)
            return d.code;
    for (const auto &d : MouseButtons)
        if (d.label == str)
            return d.code;
    for (const auto &d : GamePadButtons)
        if (d.label == str)
            return d.code;
    return 0; // 未知のキー
}

// ---------------------------------------------------------------
// キャプチャ用: 現在押されているキーボードVKを探して文字列ラベルを返す。
//   見つからなければ空文字。NONE はスキップ。
// ---------------------------------------------------------------
inline std::string_view DetectPressedKeyboardLabel()
{
    for (const auto &d : Keyboard)
    {
        if (d.code == 0)
            continue;
        if (::GetAsyncKeyState((int)d.code) & 0x8000)
        {
            return d.label;
        }
    }
    return {};
}
} // namespace InputKeyTable