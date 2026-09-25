#pragma once
// ===========================================================================
// 概要: ゲーム側が使う入力 API のインターフェースと、入力割り当て（バインディング）の型。
//
// 責務:
//   - アクション（ボタン）と軸（アナログ値）を名前で引く API を定義する
//   - マウス位置・移動量、カーソルロック、振動の API を定義する
//   - 割り当ての型（デバイス種別・キーコード・発火タイミング）を定義する
//
// 備考: 実装は InputFacade。World のリソース std::shared_ptr<IInputAPI> として登録される。
// ===========================================================================
#include <string>
#include <vector>
#include "Engine/Core/Math/StringHash.h"

// 入力デバイスの種類
enum class InputDeviceType
{
    Keyboard,         // キーボード
    Mouse_Button,     // マウスボタン
    GamePad_Button,   // ゲームパッドのボタン
    GamePad_AxisLX,   // 左スティックX
    GamePad_AxisLY,   // 左スティックY
    GamePad_AxisRX,   // 右スティックX
    GamePad_AxisRY,   // 右スティックY
    GamePad_TriggerL, // 左トリガー
    GamePad_TriggerR  // 右トリガー
};

// 割り当て1件分
struct InputBinding
{
    InputDeviceType deviceType;
    unsigned int    keyCode;      // キーボードのVK_Wや、GamePad::BTN_Aなどのビットフラグ
    float           scale = 1.0f; // 軸の方向や感度（Wなら1.0、Sなら-1.0等）
};

// アクションの発火タイミング（InputEditorWindow で設定する）
enum class TriggerType
{
    Down,      // 押しっぱなし（押されている間ずっと true）
    Triggered, // 押した瞬間だけ true
    Released   // 離した瞬間だけ true
};

// 文字列 <-> TriggerType （JSON保存/読込用）
inline const char *TriggerTypeToString(TriggerType t)
{
    switch (t)
    {
    case TriggerType::Triggered:
        return "Triggered";
    case TriggerType::Released:
        return "Released";
    case TriggerType::Down:
    default:
        return "Down";
    }
}
inline TriggerType StringToTriggerType(const std::string &s)
{
    if (s == "Triggered") return TriggerType::Triggered;
    if (s == "Released") return TriggerType::Released;
    return TriggerType::Down;
}

// 直近に操作されたデバイス（UI のアイコン表示の切り替え用）
enum class ActiveDeviceType
{
    KeyboardMouse,
    GamePad
};

class IInputAPI
{
public:
    virtual ~IInputAPI() = default;

    // 割り当てを JSON から読み直す（エディタのホットリロードから呼ぶ）
    virtual bool LoadConfig(const std::string &filePath) = 0;

    // 現在使用中のデバイスカテゴリを取得
    virtual ActiveDeviceType GetActiveDeviceType() const = 0;

    // マウスとスクリーン情報の取得
    virtual float GetMouseX() const = 0;
    virtual float GetMouseY() const = 0;
    // マウスの毎フレーム移動量（デルタ、ピクセル単位）。TPSの視点回転などに使う。
    // スティックと違い「倒し量」ではなく既に「移動量」なので、利用側で dt を掛けないこと。
    virtual float GetMouseDeltaX() const  = 0;
    virtual float GetMouseDeltaY() const  = 0;
    virtual float GetScreenWidth() const  = 0;
    virtual float GetScreenHeight() const = 0;

    // --- マウスカーソルのロック（マウスルック） ---
    //   lock=true でカーソルを画面中央へ固定して非表示にし、画面端で止まらないようにする。
    //   既定の実装は何もしない。
    virtual void SetMouseLock(bool lock) { (void)lock; }
    virtual bool IsMouseLocked() const { return false; }

    // ゲーム側がマウス視点入力を受け付けてよいか（エディタが毎フレーム更新する。エディタ無しの構成では常に true）。
    //   カーソルロックの判定にも使う：エディタの別ウィンドウ操作中はカーソルを固定しない。
    virtual bool IsGameInputActive() const { return true; }

    // --- 割り当ての登録 ---
    virtual void AddActionBinding(const std::string &actionName, InputDeviceType device, unsigned int keyCode)                 = 0;
    virtual void AddAxisBinding(const std::string &axisName, InputDeviceType device, unsigned int keyCode, float scale = 1.0f) = 0;

    // アクションの発火種別を設定（LoadConfig / エディタから使用）
    virtual void SetActionTriggerType(const std::string &actionName, TriggerType type) = 0;

    // ============================================================
    // 入力の取得
    //   名前のハッシュで引く。GetAction は、エディタで設定された種別
    //   (Down / Triggered / Released) に従って評価される。
    // ============================================================
    virtual bool  GetAction(uint32_t actionHash) const = 0;
    virtual float GetAxis(uint32_t axisHash) const     = 0;

    // 名前で呼べる版（内部でハッシュ化する）
    bool GetAction(const char *actionName) const
    {
        return GetAction(CCL::Utils::HashString(actionName));
    }
    float GetAxis(const char *axisName) const
    {
        return GetAxis(CCL::Utils::HashString(axisName));
    }

    // ============================================================
    // 表示用：アクションに割り当てられたキーを引く
    //   操作説明 UI が「Attack は何のボタンか」をアイコンに変換するために使う。
    //   UI 側でアイコンを直接持たせない理由: InputConfig.json でキーを変えたときに
    //   説明だけが古い操作を出し続けるため。割り当ての情報源は1つに保つ。
    // ============================================================

    /**
     * @brief アクションに割り当てられたバインドのうち、指定デバイス側の「代表1件」を返す。
     * @param device KeyboardMouse なら Keyboard/Mouse_Button、GamePad ならパッド系から選ぶ。
     * @param outBinding 見つかった割り当て。false のときは書き換えない。
     * @return 割り当てが1件も無ければ false（そのデバイスでは操作できないアクション）。
     * @note 複数割り当てがある場合は「先に登録されたもの」を代表とする。JSON の記載順が
     *       そのまま優先順になるので、見せたい方を上に書けばよい。
     */
    virtual bool GetPrimaryBinding(uint32_t actionHash, ActiveDeviceType device,
                                   InputBinding &outBinding) const = 0;

    bool GetPrimaryBinding(const char *actionName, ActiveDeviceType device,
                           InputBinding &outBinding) const
    {
        return GetPrimaryBinding(CCL::Utils::HashString(actionName), device, outBinding);
    }

    // コントローラーを振動させる。強さは 0..1、duration は秒
    virtual void SetVibration(float leftMotor, float rightMotor, float duration) = 0;
};