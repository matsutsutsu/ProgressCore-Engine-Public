#pragma once
// 「この UIImage は、どの操作のアイコンか」を宣言するタグ。
//
// 設計方針:
//   - これを付けた UIImage は InputIconSystem が持ち替えに応じて texturePath を張り替える。
//     操作説明・メニューの決定表示・チュートリアルなど、アイコンを出す場所すべてが
//     同じ1本の解決経路を通るので、画面ごとに対応表を複製せずに済む。
//   - Action は InputConfig.json の割り当てから導出する（＝リバインドに自動追従）。
//     ここでアイコンのパスを直接持たせないのが要点で、持たせると設定を変えた瞬間に
//     説明だけが古い操作を出し続ける「嘘をつくUI」になる。
#include <cstdint>

// アイコンの決め方。
enum class InputIconKind : std::uint8_t
{
    // InputConfig.json の割り当てから導出する（actionHash を使う）。
    Action,

    // 移動（キーマウ=方向キー / パッド=左スティック）。
    //
    // 軸は「Dキー」「Aキー」と個別に割り当てられているため、割り当てから
    // 導出すると1方向ぶんのキーしか出せない。移動は「4方向に動ける」ことが
    // 伝わればよいので、厳密さより読みやすさを取って合成アイコンを固定で使う。
    Move,

    // カメラ（キーマウ=マウス移動 / パッド=右スティック）。Move と同じ理由で固定。
    Camera,

    // メニューの決定。
    //
    // UIInputSystem がキーを直接持っている（InputConfig.json を通らない）ため、
    // ここも固定。メニューの決定キーはリバインドする性質のものではない。
    // UIInputSystem 側のキーを変えたときは InputIconSystem の対応表も直すこと。
    MenuConfirm,

    // メニューの上下選択。MenuConfirm と同じ理由で固定。
    MenuNavigate,
};

// アイコンを出す UIImage に付けるタグ。
struct InputIconComponent
{
    InputIconKind kind = InputIconKind::Action;

    // Kind::Action のときだけ使う。アクション名のハッシュ。
    // 毎フレームの文字列ハッシュを避けるため、生成時に一度だけ計算して持つ。
    std::uint32_t actionHash = 0;
};
