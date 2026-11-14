#pragma once
// ムービー1本分の台本（どう映すか）と、JSON の読み書き
#include "Engine/Cinematic/CameraShot.h"
#include "Engine/Cinematic/CinematicTracks.h"

#include <string>
#include <vector>

// 台本を置くフォルダ。直下の .json をすべて読む
inline constexpr const char *kCinematicScriptDir = "Assets/Cinematics";

struct CinematicScript
{
    std::string path;             // 台本のパス。再生要求と終了の知らせで台本を指す名前
    float       duration  = 5.0f; // 再生の長さ（秒）
    bool        allowSkip = true; // 長押しでスキップできるか

    // --- 再生側は時刻の昇順を前提にする（SortByTime） ---
    std::vector<CameraShot>         shots;      // ショット
    std::vector<CinematicSound>     sounds;     // SE
    std::vector<CinematicEffect>    effects;    // エフェクト
    std::vector<CinematicSignal>    signals;    // シグナル
    std::vector<CinematicAnimation> animations; // アニメ
};

namespace CinematicScriptIO
{
    // 台本の JSON を読み、時刻の順に並べる。書いていない項目は既定値になる
    // 読めたときだけ out を書き換える。読めなければ理由をログに出して false
    bool Load(const std::string &path, CinematicScript &out);

    // 台本を JSON へ書き出す。書けなければ false
    bool Save(const std::string &path, const CinematicScript &script);

    // ショットとトラックを時刻の順に並べ直す。同じ時刻は書いた順を保つ
    void SortByTime(CinematicScript &script);
} // namespace CinematicScriptIO
