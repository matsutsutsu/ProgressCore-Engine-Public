#pragma once

// ゲーム進行の共有状態（World リソース）
// ポーズ・UI 操作中・ムービー中・終了要求を System 間で共有する
struct GameFlowState
{
    // --- 状態 ---
    // ムービー中のポーズのように重なるので、enum でなく別々の bool で持つ

    // ポーズ中。ロジックと物理を止め、描画と UI だけ動かす
    // FixedUpdateを止めてUpdateは動かす
    bool paused = false;

    // UI 操作モード（F2・シーケンサーの編集中）。ゲームは止めずにカーソルを解放する
    //   エディタが毎フレーム上書きする
    bool uiInteract = false;

    // ムービーの開始待ち・再生中。ゲーム自体は止めない
    //   描画の段で書くので、ロジックには次の固定ステップから効く
    // 　一部のプレイヤーや敵AIの入力を阻止するために使うフラグ
    bool cinematic = false;

    // 決着してから、シーンを切り替えるまで。ゲーム自体は止めない
    bool matchOver = false;

    // 決着の画面（リザルト・チュートリアル完了）を出してから、シーンを切り替えるまで
    bool resultOpen = false;

    // アプリの終了要求。タイトルの「終了」だけが立てる
    //   System からは PostQuitMessage を呼ばずにこれを立てる
    bool quitRequested = false;

    // --- 組み合わせ ---
    // 読み手ごとに条件を書くと、フラグを足したときに直し漏れが出るのでここにまとめる

    // プレイを止めるか（ムービー中・決着後）。AI・ボスの技・視点の入力を止める
    bool GameplayFrozen() const { return cinematic || matchOver; }

    // プレイヤーの操作を止めるか。上に UI 操作モードを足したもの
    //   UI 操作モードの間も敵は動かす。動いている戦闘を見ながら調整するため
    bool PlayerInputBlocked() const { return GameplayFrozen() || uiInteract; }

    // カーソルを解放するか（ポーズ・UI 操作モード・決着の画面）
    //   matchOver だけでは解放しない。とどめのスローとムービーの間に出てしまう
    bool CursorFree() const { return paused || uiInteract || resultOpen; }
};
