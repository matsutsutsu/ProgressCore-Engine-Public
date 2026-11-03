#pragma once
// ===========================================================================
// 概要: UIImage に付けて「どの操作のアイコンを出すか」を指定するコンポーネント。
//
// 責務:
//   - アイコンの種類と、アクション名のハッシュを持つ
//
// 備考: 画像のパスは持たせず、InputIconSystem が今のデバイスと割り当てから決める。
//       InputConfig.json の割り当てを変えたときに、説明だけ古い操作を出し続けないようにするため。
// ===========================================================================
#include <cstdint>

// アイコンの決め方
enum class InputIconKind : std::uint8_t
{
    // InputConfig.json の割り当てから決める（actionHash を使う）
    Action,

    // 移動（キーボードとマウスなら方向キー、パッドなら左スティック）。固定のアイコンを使う。
    // 軸は D キー・A キーのように1方向ずつ割り当てるので、割り当てから決めると1方向ぶんしか出せない。
    // 4方向に動けることが伝わればよいので、4方向をまとめたアイコンにする
    Move,

    // カメラ（キーボードとマウスならマウス移動、パッドなら右スティック）。Move と同じ理由で固定
    Camera,

    // メニューの決定。UIInputSystem がキーを直接持っていて InputConfig.json を通らないので固定。
    // UIInputSystem のキーを変えたら、InputIconSystem の対応表も直すこと
    MenuConfirm,

    // メニューの上下の選択。MenuConfirm と同じ理由で固定
    MenuNavigate,

    // ポーズ（ESC / START）。PauseSystem がキーを直接見ていて InputConfig.json を通らないので固定
    Pause,
};

struct InputIconComponent
{
    InputIconKind kind = InputIconKind::Action;

    // アクション名のハッシュ（kind が Action のときだけ使う）。
    // 毎フレーム文字列からハッシュを求めないよう、生成時に1回だけ求めて持つ
    std::uint32_t actionHash = 0;
};
