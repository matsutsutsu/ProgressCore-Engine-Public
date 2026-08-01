#pragma once
/// @file GameFlowState.h
/// @brief ゲーム進行の共有状態（World リソース）。
///        シーン更新のゲート（ポーズ）に使う。
struct GameFlowState
{
    /// true の間、ゲームロジック/物理（FixedUpdate=UpdateLogic）を止める。
    /// 描画・UI（Update=UpdatePaused）は継続するので、ポーズ画面が操作できる。
    bool paused = false;

    /// ★ 「カーソル自由(UI操作)モード」。true の間はゲームを止めずに
    ///    マウスカーソルだけ解放する（＝実行中に ImGui エディタを操作できる）。
    ///    ・カメラ: この間はカーソルをロックせず、マウスルックも止める。
    ///    ・PauseSystem: この間は ESC のポーズ切替を抑制する。
    ///    エディタ側(F2トグル / シーケンサーEditMode)が毎フレーム設定する。
    bool uiInteract = false;

    /// ★ アプリ終了要求。ポーズメニューの QUIT から立てる。
    ///    重要: システムの Update はジョブスケジューラによりワーカースレッドで走るため、
    ///    ワーカースレッドから PostQuitMessage を呼ぶと WM_QUIT が別スレッドのキューに積まれ、
    ///    メインループ(PeekMessage=メインスレッド)が永久に拾えず終了しない。
    ///    そこで「フラグを立てるだけ」にして、メインスレッドの Run() ループが毎フレーム
    ///    これを見て PostQuitMessage(0) を呼ぶ。（ジョブの WaitForCounter が同期点になるので
    ///    書き込みは main へ確実に可視化される）
    bool quitRequested = false;
};
