#pragma once
// UI階層のルート「キャンバス」を表すマーカー。矩形は常に画面全体になる。
//
// 設計方針:
//   - UI の親子は既存の TransformComponent(parent/firstChildID) を再利用する。これにより
//     エディタの HierarchyWindow でそのまま入れ子表示され、ドラッグ&ドロップの親子付けも効く。
//   - Canvas を持つエンティティの UI 矩形は「画面全体」を基準にしつつ、自身の UITransform の
//     offset で平行移動・scale で拡縮する“可動ルート”（UILayoutUtil::ResolveUINode が特別扱い）。
//     Canvas を動かす/拡縮すると配下UIがまとめて追従する。size/anchor/pivot は無視される。
//   - スクリーンスペース固定。ワールドスペースCanvas等は将来の拡張余地として持たない（YAGNI）。
struct UICanvasComponent
{
    // 現状はマーカーのみ。将来: 参照解像度・描画順オフセット等をここへ。
    int sortBase = 0; // 配下UIの描画順の基準（未使用・拡張余地）
};
