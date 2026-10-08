#pragma once
// ===========================================================================
// 概要: ゲーム進行の共有状態（World リソース）GameFlowState。
//
// 責務:
//   - ポーズ・UI 操作モード・カットシーン中・終了要求のフラグを System 間で共有する
// ===========================================================================
struct GameFlowState
{
    // true の間、ゲームロジック/物理（FixedUpdate = UpdateLogic）を止める。
    // 描画・UI（Update = UpdatePaused）は継続するので、ポーズ画面が操作できる。
    bool paused = false;

    // 「カーソル自由(UI操作)モード」。true の間はゲームを止めずに
    //    マウスカーソルだけ解放する（＝実行中に ImGui エディタを操作できる）。
    //    ・カメラ: この間はカーソルをロックせず、マウスルックも止める。
    //    ・PauseSystem: この間は ESC のポーズ切替を抑制する。
    //    エディタ側（F2 トグル / シーケンサーの EditMode）が毎フレーム設定する。
    bool uiInteract = false;

    // 「カットシーン中」。タイムライン（ムービー）が開始待ち・再生中の間 true。
    //    ・PlayerInputSystem: 全ゲーム入力を中立値にして操作を奪う。
    //    ・BehaviorTreeSystem: 木を凍らせ、演出中に敵が動き出さないようにする。
    //    ・GameResultSystem: 勝ちのリザルトを、撃破ムービーが終わるまで出さない。
    //    paused と違ってゲーム自体は止めない（アニメも物理もカメラも動き続ける）。
    //    書き手は TimelineSystem だけ（ロジックの L01_Input）。
    //    PlayerInputSystem とは同じ段で順序を縛っていないので、効き始めが1ステップ遅れることがある。
    bool cinematic = false;

    // 「決着した」。勝ち負けが決まった瞬間から、シーンを切り替えるまで true。
    //    ・PlayerInputSystem: 全ゲーム入力を中立値にする（リザルトの裏でプレイヤーが動かないように）。
    //    ・BehaviorTreeSystem / BossActionSystem: ボスの思考と技を止める（倒れたプレイヤーへ攻撃し続けないように）。
    //    ・CameraActionTPSSystem: 視点入力を止める（リザルトをマウスで操作している間にカメラが回らないように）。
    //    cinematic と同じく、ゲーム自体は止めない（倒れるアニメや物理は動き続ける）。
    //    書き手は GameResultSystem だけ。
    bool matchOver = false;

    // アプリ終了要求。タイトル画面の QUIT から立てる（ゲーム中のポーズ／リザルトは
    //    タイトルへ戻すだけで、アプリを終了する出口はタイトルの1か所だけにしている）。
    //    System の Update はジョブスケジューラによりワーカースレッドで走るため、
    //    ワーカースレッドから PostQuitMessage を呼ぶと WM_QUIT が別スレッドのキューに積まれ、
    //    メインループ（PeekMessage = メインスレッド）が永久に拾えず終了しない。
    //    そこで「フラグを立てるだけ」にして、メインスレッドの Run() ループが毎フレーム
    //    これを見て PostQuitMessage(0) を呼ぶ。（ジョブの WaitForCounter が同期点になるので
    //    書き込みは main へ確実に可視化される）
    bool quitRequested = false;
};
