#pragma once
#include <cstdint>

// アイコンの決め方
enum class InputIconKind : std::uint8_t
{
    // InputConfig.json の割り当てから決める（actionHash を使う）
    Action,

    // 移動（キーボードとマウスなら方向キー、パッドなら左スティック）。固定のアイコンを使う
    // 軸は D キー・A キーのように1方向ずつ割り当てるので、割り当てから決めると1方向ぶんしか出せない
    // 4方向に動けることが伝わればよいので、4方向をまとめたアイコンにする
    Move,

    // カメラ（キーボードとマウスならマウス移動、パッドなら右スティック）。Move と同じ理由で固定
    Camera,

    // メニューの決定。UIInputSystem がキーを直接持っていて InputConfig.json を通らないので固定
    // UIInputSystem のキーを変えたら、InputIconSystem の対応表も直すこと
    MenuConfirm,

    // メニューの上下の選択。MenuConfirm と同じ理由で固定
    MenuNavigate,

    // ポーズ（ESC / START）。PauseSystem がキーを直接見ていて InputConfig.json を通らないので固定
    Pause,

    // 前・後ろへの移動（キーボードなら W / S、パッドなら左スティックの上 / 下）
    // コンボの派生のように、倒す向きで技が変わる所で使う。Move の4方向の絵では向きが伝わらない
    // 番号はプレハブに保存されるので、新しい種類は末尾に足す
    MoveForward,
    MoveBack,
};

// UIImage に付けて、どの操作のアイコンを出すかを指定する
// 画像のパスは持たず、InputIconSystem が今のデバイスと割り当てから決める
struct InputIconComponent
{
    InputIconKind kind = InputIconKind::Action;

    // アクション名のハッシュ（kind が Action のときだけ使う）
    // 毎フレーム文字列からハッシュを求めないよう、生成時に1回だけ求めて持つ
    std::uint32_t actionHash = 0;
};
