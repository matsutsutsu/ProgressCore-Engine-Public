#pragma once

// 付けた Entity を、通常描画とシャドウ描画の両方から外すタグ。見えない壁や床などの当たり判定だけの形に使う
// VisibilityComponent.isVisible はカリングが毎フレーム上書きするので、ずっと隠すならこのタグを使う
struct NoRenderTag
{
};
