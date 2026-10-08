#pragma once
// ===========================================================================
// 概要: UI 階層のルート（キャンバス）を示すマーカーコンポーネント UICanvasComponent。
//
// 責務:
//   - この Entity を UI 階層のルートとして扱わせる
//
// 備考:
//   - UI の親子は TransformComponent の親子をそのまま使う。
//     そのためエディタの HierarchyWindow で入れ子表示され、ドラッグ&ドロップで親子付けもできる。
//   - Canvas の矩形は画面全体を基準に、自身の UITransform の offset で平行移動、scale で拡縮する
//     （UILayoutUtil::ResolveUINode が特別扱いする）。Canvas を動かすと配下の UI がまとめて動く。
//     size / anchor / pivot は無視される。
//   - スクリーンスペースのみ。ワールドスペースの Canvas は持たない。
// ===========================================================================
struct UICanvasComponent
{
};
