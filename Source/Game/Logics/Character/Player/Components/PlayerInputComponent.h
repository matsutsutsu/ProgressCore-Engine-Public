#pragma once
// ===========================================================================
// 概要: プレイヤーのボタン操作を「このステップで押された瞬間か」まで判定した結果を持つ Component。
//
// 責務:
//   - 攻撃・ジャンプ・回避・切り払い・ロックオンの「押した瞬間」を持つ
//   - 押した瞬間の判定に使う前ステップの押下状態を持つ
//
// 備考:
//   書くのは PlayerInputSystem だけで、各アクションの System は読むだけ。
//
//   押した瞬間の判定をここで1回だけ行う理由:
//   入力の取得は描画フレームに1回だが、ロジック(FixedUpdate)は同じフレーム内で
//   0〜4 回走る（Framework::Run の MAX_STEPS）。そのため「押した瞬間」を表す
//   GetAction の true は、同じフレームの全ステップで true のままになる。
//   各 System が個別に判定すると、
//     ・状態で二重発動を防げる処理（攻撃/ダッシュ/切り払い）は結果的に問題が出ない
//     ・防ぐ手段が無い処理（ロックオンの切り替え）は ON→OFF して何も起きない
//   というように、System によって結果が変わる。判定を1箇所にまとめ、全アクションを同じ規則で動かす。
//
//   移動方向はここに持たない。AI も同じ経路で動かすため、
//   CharacterMovementComponent.desiredVelocity を共通の入口にしている。
// ===========================================================================

struct PlayerInputComponent
{
    // --- このステップの入力（PlayerInputSystem が毎ステップ書く） ---
    bool attackPressed = false; // 攻撃（左クリック）を押した瞬間
    bool jumpPressed   = false; // ジャンプを押した瞬間
    bool dashPressed   = false; // 回避ダッシュを押した瞬間
    bool cleavePressed = false; // 切り払いの準備（右クリック）を押した瞬間
    bool lockOnPressed = false; // ロックオンのトグルを押した瞬間

    // --- 押した瞬間の判定に使う前ステップの押下状態（PlayerInputSystem 専用） ---
    //
    // UI操作やカットシーンで入力を止めている間も、実際の押下状態をここへ書き続ける。
    // 書くのを止めると、入力が戻った瞬間に押しっぱなしのボタンが「押した瞬間」と判定され、
    // 意図しないアクションが出る。
    bool prevAttack = false;
    bool prevJump   = false;
    bool prevDash   = false;
    bool prevCleave = false;
    bool prevLockOn = false;
};
