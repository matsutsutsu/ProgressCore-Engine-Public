#pragma once
// ムービーの再生要求・停止要求・終了・シグナル・台本の書き換えのイベント
// 再生したい側はムービー用カメラを知らず、要求を出すだけ。受け付けるかは CinematicCameraSystem が決める
#include <memory>
#include <string>

struct CinematicScript;

// ムービーの再生要求。再生中に届いた要求と、同じフレームに2つ目以降に届いた要求は捨てる
struct CinematicPlayRequestEvent
{
    std::string                            scriptPath;       // 流す台本のパス
    float                                  startTime = 0.0f; // 再生を始める位置（秒）。0 より後なら開始待ちを飛ばし、それより前のトラックの点は出さない
    std::shared_ptr<const CinematicScript> script;           // 台本の中身を直接渡すとき（エディタの未保存の写し）。あれば scriptPath で探さない
};

// 流しているムービーを止める要求（エディタの停止）。スキップと同じく終わりの知らせを出す
struct CinematicStopRequestEvent
{
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

// 台本のファイルが書き換わった知らせ（エディタの保存）。CinematicCameraSystem が台本を読み直す
struct CinematicScriptsChangedEvent
{
};
