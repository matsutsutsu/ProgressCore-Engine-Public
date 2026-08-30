#pragma once
// ===========================================================================
// PlayerInputComponent.h
// ----------------------
// 役割:
//   プレイヤーの操作ボタンを「このフレームに押された(エッジ)」まで解決した意思。
//   PlayerInputSystem だけが書き、各アクションシステムは読むだけ。
//
// なぜエッジをここで作るか:
//   ハードウェア入力の取得は描画フレームに1回だが、ロジック(FixedUpdate)は
//   同じフレーム内で 0〜4 回走る（Framework の可変フレーム対応）。つまり
//   「押した瞬間」を表す GetAction の true は、同じフレームの全ステップで
//   true のままになる。各システムが個別にエッジを取ると、
//     ・状態で二重発火を弾ける処理（攻撃/ダッシュ/切り払い）は偶然助かる
//     ・弾く材料が無い処理（ロックオンのトグル）は ON→OFF して何も起きない
//   という「システムによって効いたり効かなかったり」が生まれる。
//   押下の解釈を1箇所へ集約し、全アクションが同じ規則で動くようにする。
//
// 設計方針:
//   - 全て POD。prev* は非公開扱いの内部状態で、PlayerInputSystem 以外は触らない。
//   - 移動方向だけはここに持たない。AI も同じ経路で動かすため、
//     CharacterMovementComponent.desiredVelocity が共通の窓口になる。
// ===========================================================================

/// @brief プレイヤーのボタン操作を「押した瞬間」まで解決して保持する。
struct PlayerInputComponent
{
    // --- このフレームの意思（PlayerInputSystem が毎ステップ確定させる） ---
    bool attackPressed = false; ///< 攻撃（左クリック）を押した瞬間
    bool jumpPressed   = false; ///< ジャンプを押した瞬間
    bool dashPressed   = false; ///< 回避ダッシュを押した瞬間
    bool cleavePressed = false; ///< 切り払いの準備（右クリック）を押した瞬間
    bool lockOnPressed = false; ///< ロックオンのトグルを押した瞬間

    // --- エッジ検出のための前ステップ状態（PlayerInputSystem 専用） ---
    /// @note UI操作やカットシーンで入力を遮断している間も、生の押下状態はここへ
    ///       追随させる。追随を止めると、遮断が解けた瞬間に「押しっぱなし」が
    ///       立ち上がり扱いになってアクションが暴発する。
    bool prevAttack = false;
    bool prevJump   = false;
    bool prevDash   = false;
    bool prevCleave = false;
    bool prevLockOn = false;
};
