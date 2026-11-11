#pragma once
// ===========================================================================
// 概要: ムービー1本分の台本（どう映すか）のデータと、JSON の読み書き
//
// 備考: 書き間違い（知らない項目・型の違い・範囲外の値）があれば、台本ごと読み込まずにログへ出す
//       既定値で通すと、書いたつもりの値が使われていないことに気付けない。項目の省略は既定値を使う
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
    // 台本の JSON を読む。読めたときだけ out を書き換える。ファイルがない・書き間違いがあるときは理由をログに出して false
    bool Load(const std::string &path, CinematicScript &out);

    // 台本を JSON へ書き出す。読み込みと同じ決まりで確かめ、次に読めない台本になるなら書かずに false（理由は err）
    // 項目は既定値と同じでも省かずに書く。_comment は書かない
    bool Save(const std::string &path, const CinematicScript &script, std::string &err);
} // namespace CinematicScriptIO
