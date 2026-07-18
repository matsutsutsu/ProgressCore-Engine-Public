#pragma once
/// @file GameFlowState.h
/// @brief ゲーム進行の共有状態（World リソース）。
///        シーン更新のゲート（ポーズ）に使う。
struct GameFlowState
{
    /// true の間、ゲームロジック/物理（FixedUpdate=UpdateLogic）を止める。
    /// 描画・UI（Update=UpdatePaused）は継続するので、ポーズ画面が操作できる。
    bool paused = false;
};
