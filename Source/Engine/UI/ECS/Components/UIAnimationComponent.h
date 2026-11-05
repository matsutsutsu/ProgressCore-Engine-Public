#pragma once
// UI のスケールを目標値へ滑らかに追従させる。目標値は使う側が書く（UIButtonSystem が hover 中に書くなど）
// 追従は UIAnimationSystem が指数減衰で行うので、フレームレートによらない
struct UIAnimationComponent
{
    float smoothSpeed = 12.0f; // 追従の速さ（1 秒あたり。大きいほど速く目標へ寄る）
    float targetScale = 1.0f;  // UITransform.scale の目標値
};
