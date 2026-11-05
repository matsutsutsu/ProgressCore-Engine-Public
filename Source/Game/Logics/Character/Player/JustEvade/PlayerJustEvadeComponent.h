#pragma once

// ジャスト回避の成立状態。受付中かどうかは PlayerDashComponent が持つ
struct PlayerJustEvadeComponent
{
    bool isJustSuccess = false; // 今のダッシュで成立済みか
};
