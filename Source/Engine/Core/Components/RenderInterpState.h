#pragma once

// 固定ステップの物理と可変フレームの描画をつなぐ補間係数（World リソース）。Framework が書き、描画の System が読む
// alpha = accumulator / FIXED_DT。最初に1回だけ確保し、以降はその場で書き換えるので毎フレームの確保はない
struct RenderInterpState
{
    float alpha = 1.0f; // 次の固定ステップに対する進行率 [0.0, 1.0]
};