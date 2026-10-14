#pragma once
// ===========================================================================
// 概要: ムービー1本分の台本（どう映すか）のデータと、JSON からの読み込み。
//
// 責務:
//   - 再生の長さ・開始待ち・スキップ可否と、ショットの列を持つ
//   - カメラ以外のトラック（SE・エフェクト・シグナル・アニメ）を持つ
//   - Assets/Cinematics の JSON を読み、書き間違いがあれば読み込まずにログへ出す
//
// 備考:
//   書き間違い（知らない項目・型の違い・知らない名前・範囲外の値）は、台本ごと読み込まない。
//   既定値で通すと、書いたつもりの値が使われていないことに気付けないため。
//   項目の省略は書き間違いではないので、既定値を使う。
// ===========================================================================
#include "Engine/Cinematic/CameraShot.h"
#include "Engine/Cinematic/CinematicTracks.h"

#include <string>
#include <vector>

// 台本を置くフォルダ。直下の .json をすべて読む
inline constexpr const char *kCinematicScriptDir = "Assets/Cinematics";

struct CinematicScript
{
    std::string path; // 読み込んだファイルのパス。再生要求と終了の知らせで台本を指す名前

    float duration   = 5.0f; // 再生の長さ（秒）
    float startDelay = 0.0f; // 最初の絵で止めて、動き出すまで待つ時間（秒）
    bool  allowSkip  = true; // 長押しでスキップできるか

    std::vector<CameraShot> shots; // 映すショットの列。開始時刻の昇順

    // --- カメラ以外のトラック。点は時刻の昇順、アニメは開始時刻の昇順 ---
    std::vector<CinematicSound>     sounds;     // SE
    std::vector<CinematicEffect>    effects;    // エフェクト
    std::vector<CinematicSignal>    signals;    // シグナル
    std::vector<CinematicAnimation> animations; // アニメ
};

namespace CinematicScriptIO
{
    /**
     * @brief 台本の JSON を読む。
     * @param path 読むファイルの仮想パス。
     * @param out  読めたときだけ書き換える。
     * @return 読めたら true。ファイルが無い・書き間違いがあるときは、理由をログへ出して false。
     */
    bool Load(const std::string &path, CinematicScript &out);
} // namespace CinematicScriptIO
