#pragma once
// ===========================================================================
// 概要: ムービーの台本のリップル編集。ショットの終わりを動かした分だけ、それより後ろの項目と全体の長さをずらす
//
// 責務:
//   - ショットを動かし始める前に、後ろの項目（ショット・SE・エフェクト・シグナル・アニメ）の元の時刻を控える
//   - 動かした後のショットの終わりと、控えた終わりの差だけ、控えた項目と全体の長さをずらす
//
// 備考: 毎回「控えた元の時刻 + ずれ」で書き直す。その場の値にずれを足していくと、縮めたときに
//       ショットの途中にあった項目へ終わりの端が追いつき、そこから先は一緒に押してしまう
//       「後ろ」は、ショットの元の終わり以降に始まるもの。切り替わりちょうどにある SE なども一緒にずれる
//       ショットの途中にある項目はずらさない（そのショットの中の出来事なので、ショットの頭からの時刻を保つ）
//       左端だけを動かしたときは終わりが変わらないので、何もずれない
// ===========================================================================
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
        float       originEnd      = 0.0f;  // 控えたときの、そのショットの終わり（秒）
        float       originDuration = 0.0f;  // 控えたときの、全体の長さ（秒）

        // 後ろの項目
        TimeList shots;
        TimeList sounds;
        TimeList effects;
        TimeList signals;
        TimeList animations;
    };

    // shotIndex のショットより後ろにある項目の、今の時刻を控える
    // ショットがなければ valid が false の控えを返す
    Snapshot Capture(const CinematicScript &script, std::size_t shotIndex);

    // 今のショットの終わりと控えた終わりの差だけ、控えた項目と全体の長さをずらす
    // 控えがなければ何もしない
    void Apply(CinematicScript &script, const Snapshot &snapshot);
} // namespace CinematicRipple
