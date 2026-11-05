#pragma once
// UI 階層のルート（キャンバス）を示す目印。UI の親子は TransformComponent の親子をそのまま使う
// Canvas の矩形は画面全体を基準に、offset で動き scale で拡縮する。size / anchor / pivot は無視する
struct UICanvasComponent
{
};
