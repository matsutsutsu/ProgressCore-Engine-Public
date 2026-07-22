#pragma once
/// @file GameViewportInfo.h
/// @brief 「ゲーム画面(RT)が実際に表示されている矩形」をエンジンへ橋渡しする World リソース。
///
/// 目的:
///   UI のクリック判定(UIInputSystem)は、マウスの「ウィンドウ・クライアント座標(px)」を
///   UI のオーサリング空間(=ゲームRT解像度 Graphics::GetScreenWidth/Height)へ写像する必要がある。
///   全画面(F1)では両者が 1:1 で一致するが、エディタの GameView ではゲームRTがレターボックス＋
///   縮小されて小さな画像として貼られるため、原点も倍率もズレる。
///
/// 設計:
///   - エディタ(GameViewWindow)が毎フレーム、貼っているゲーム画像の矩形(x,y,w,h)を
///     「クライアント座標」で書き込み、valid=true にする。
///   - UIInputSystem は valid のときだけ (mouse - {x,y}) / {w,h} * screenW/H で写像する。
///   - Shipping / 全画面では誰も書き込まない → valid=false のまま → 恒等写像(従来挙動)。
///   - よってエンジン側はエディタ非依存・ImGui非依存で、#ifdef も不要。
///
/// 座標系:
///   x,y,w,h はプラットフォーム Mouse::GetPositionX/Y と同じ「メインウィンドウのクライアントpx」。
///   ImGui マルチビューポートでスクリーン座標が OS デスクトップ基準になるため、GameViewWindow 側で
///   メインビューポート原点を引いてクライアント基準へ直してから書き込むこと。
struct GameViewportInfo
{
    float x = 0.0f; ///< ゲーム画像の左上X（ウィンドウ・クライアントpx）
    float y = 0.0f; ///< ゲーム画像の左上Y（ウィンドウ・クライアントpx）
    float w = 0.0f; ///< ゲーム画像の表示幅（レターボックス後の実サイズpx）
    float h = 0.0f; ///< ゲーム画像の表示高（レターボックス後の実サイズpx）
    bool  valid = false; ///< エディタで GameView にRTが貼られているフレームだけ true
};
