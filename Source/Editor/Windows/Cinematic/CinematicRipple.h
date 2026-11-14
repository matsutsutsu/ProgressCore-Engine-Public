#pragma once
// ムービーの台本のリップル編集。ショットの終わりを動かした分だけ、後ろの項目と全体の長さをずらす
// 毎回「控えた元の時刻 + ずれ」で書き直す。その場の値に足していくと、縮めたときに途中の項目まで押してしまう
#include <cstddef>
#include <utility>
#include <vector>

struct CinematicScript;

namespace CinematicRipple
{
    // 種類ごとの控え。（配列の添字, 元の時刻）の並び
    using TimeList = std::vector<std::pair<std::size_t, float>>;

    // 動かし始める前の控え
    struct Snapshot
    {
        bool        valid          = false; // 控えがあるか
        std::size_t shotIndex      = 0;     // 動かすショットの添字
        float       originEnd      = 0.0f;  // 控えたときの、ショットの終わり（秒）
        float       originDuration = 0.0f;  // 控えたときの、全体の長さ（秒）

        // --- ショットの元の終わり以降に始まる項目 ---
        TimeList shots;
        TimeList sounds;
        TimeList effects;
        TimeList signals;
        TimeList animations;
    };

    // shotIndex のショットより後ろにある項目の、今の時刻を控える
    Snapshot Capture(const CinematicScript &script, std::size_t shotIndex);

    // 今のショットの終わりと控えた終わりの差だけ、控えた項目と全体の長さをずらす
    void Apply(CinematicScript &script, const Snapshot &snapshot);
} // namespace CinematicRipple
