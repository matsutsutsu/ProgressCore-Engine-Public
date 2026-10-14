#pragma once
// ===========================================================================
// 概要: ムービーの再生に関わるイベントの定義。
//
// 責務:
//   - ムービーの再生要求を定義する
//   - ムービーの終了の知らせを定義する
//   - 台本のシグナル（ゲーム側へ渡す汎用の知らせ）を定義する
//
// 備考:
//   再生したい側（ボス撃破の検知など）はムービー用カメラの Entity を知らなくてよい。
//   要求を発行するだけで、受け付けるかどうかは CinematicCameraSystem が決める。
// ===========================================================================
#include <string>

// ムービーの再生要求。再生中に届いた要求と、同じフレームに2つ目以降に届いた要求は捨てる
struct CinematicPlayRequestEvent
{
    std::string scriptPath; // 流す台本のパス
};

// ムービーの終了の知らせ。最後まで再生したときも、スキップしたときも1回出す
struct CinematicFinishedEvent
{
    std::string scriptPath;      // 終わった台本のパス
    bool        skipped = false; // スキップで終えたか
};

// 台本のシグナル。意味はエンジンでは決めず、受け手が name を見て解釈する
struct CinematicSignalEvent
{
    std::string name;         // シグナルの名前
    std::string param;        // 引数1（文字列）
    std::string param2;       // 引数2（文字列）
    float       value = 0.0f; // 引数3（数値）
};
