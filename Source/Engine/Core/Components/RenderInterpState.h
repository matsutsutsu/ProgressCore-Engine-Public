// RenderInterpState.h
#pragma once

// 固定ステップ物理と可変フレーム描画をつなぐ補間係数を保持する共有リソース。
// Fix Your Timestep パターンの alpha を Framework から Render系システムへ運ぶ。

/**
 * @brief 描画補間の進行率を保持する World Resource。
 * @note alpha = accumulator / FIXED_DT。AddResource で初回1回だけ確保し、以降は
 *       GetResource() が返す参照をその場更新するため、毎フレームのヒープ確保は発生しない。
 *       Resource はシリアライズ対象外なので Meta ファイルは不要。
 */
struct RenderInterpState
{
    float alpha = 1.0f; // 次の固定ステップに対する進行率 [0.0, 1.0]
};