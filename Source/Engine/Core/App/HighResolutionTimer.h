#pragma once
// ===========================================================================
// 概要: QueryPerformanceCounter による高精度タイマー HighResolutionTimer
//
// 責務:
//   - フレーム間の経過時間（TimeInterval）と、停止時間を除いた累計時間（TimeStamp）の計測
//   - 停止・再開（ウィンドウのドラッグ中などに時間を進めない）
// ===========================================================================
#include <windows.h>

class HighResolutionTimer
{
public:
    HighResolutionTimer() : delta_time(-1.0), paused_time(0), stopped(false)
    {
        LONGLONG counts_per_sec;
        QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER *>(&counts_per_sec));
        seconds_per_count = 1.0 / static_cast<double>(counts_per_sec);

        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&this_time));
        base_time = this_time;
        last_time = this_time;
    }

    // Reset() からの累計時間（秒）を返す。停止していた時間は含めない
    float TimeStamp() const
    {
        // 停止中なら、止めた時点以降の時間は数えない
        // さらに過去にも停止があった場合、stop_time - base_time にはその停止時間が
        // 含まれているので、paused_time を引いて取り除く:
        //
        //                     |<--paused_time-->|
        // ----*---------------*-----------------*------------*------------*------> time
        //  base_time       stop_time        start_time     stop_time    this_time

        if (stopped)
        {
            return static_cast<float>(((stop_time - paused_time) - base_time) * seconds_per_count);
        }

        // 動作中も、this_time - base_time には過去の停止時間が含まれているので、
        // paused_time を引いて取り除く:
        //
        //  (this_time - paused_time) - base_time
        //
        //                     |<--paused_time-->|
        // ----*---------------*-----------------*------------*------> time
        //  base_time       stop_time        start_time     this_time
        else
        {
            return static_cast<float>(((this_time - paused_time) - base_time) * seconds_per_count);
        }
    }

    // 直近の Tick 間の経過時間（秒）
    float TimeInterval() const
    {
        return static_cast<float>(delta_time);
    }

    // メッセージループの前に呼ぶ
    void Reset()
    {
        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&this_time));
        base_time = this_time;
        last_time = this_time;

        stop_time = 0;
        stopped   = false;
    }

    // 停止を解除するときに呼ぶ
    void Start()
    {
        LONGLONG start_time;
        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&start_time));

        // Stop から Start までの時間を paused_time に積み上げる
        //
        //                     |<-------d------->|
        // ----*---------------*-----------------*------------> time
        //  base_time       stop_time        start_time
        if (stopped)
        {
            paused_time += (start_time - stop_time);
            last_time = start_time;
            stop_time = 0;
            stopped   = false;
        }
    }

    // 停止するときに呼ぶ
    void Stop()
    {
        if (!stopped)
        {
            QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&stop_time));
            stopped = true;
        }
    }

    // 毎フレーム呼ぶ
    void Tick()
    {
        if (stopped)
        {
            delta_time = 0.0;
            return;
        }

        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&this_time));
        delta_time = (this_time - last_time) * seconds_per_count;

        last_time = this_time;

        // 負の値は 0 に丸める。CPU が省電力モードに入ったり、別のコアへ移されたり
        // すると、delta_time が負になることがある（DXSDK の CDXUTTimer の注記より）
        if (delta_time < 0.0)
        {
            delta_time = 0.0;
        }
    }

private:
    double seconds_per_count; // カウンタ1刻みの秒数
    double delta_time;        // 直近の Tick 間の経過時間（秒）

    // 以下はすべて QueryPerformanceCounter のカウント値
    LONGLONG base_time;   // Reset した時刻
    LONGLONG paused_time; // これまでに停止していた時間の合計
    LONGLONG stop_time;   // 最後に Stop した時刻
    LONGLONG last_time;   // 前回 Tick の時刻
    LONGLONG this_time;   // 今回 Tick の時刻

    bool stopped; // Stop されていて、時間を進めていないか
};
