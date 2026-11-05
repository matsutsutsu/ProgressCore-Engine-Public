#pragma once
// ===========================================================================
// 概要: Win32 まわりの小さなデバッグ用ユーティリティ
//
// 責務:
//   - メッセージ付き assert マクロ（_ASSERT_EXPR_A）
//   - HRESULT をエラーメッセージ文字列に変換する（HRTrace）
//   - 区間の経過時間を測る簡易タイマー（Benchmark）
// ===========================================================================
#include <windows.h>
#include <crtdbg.h>

// 式が偽ならメッセージ付きで止める。Release では何もしない
#if defined(DEBUG) || defined(_DEBUG)
#define _ASSERT_EXPR_A(expr, msg)                                                    \
    (void)((!!(expr)) ||                                                             \
           (1 != _CrtDbgReport(_CRT_ASSERT, __FILE__, __LINE__, NULL, "%s", msg)) || \
           (_CrtDbgBreak(), 0))
#else
#define _ASSERT_EXPR_A(expr, expr_str) ((void)0)
#endif

// HRESULT のエラーメッセージを返す。返した文字列は LocalFree で解放すること
inline LPWSTR HRTrace(HRESULT hr)
{
    LPWSTR msg;
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&msg), 0, NULL);
    return msg;
}

// begin() から end() までの経過時間（秒）を測る
class Benchmark
{
    LARGE_INTEGER ticksPerSecond;
    LARGE_INTEGER startTicks;
    LARGE_INTEGER currentTicks;

public:
    Benchmark()
    {
        QueryPerformanceFrequency(&ticksPerSecond);
        QueryPerformanceCounter(&startTicks);
        QueryPerformanceCounter(&currentTicks);
    }
    void begin()
    {
        QueryPerformanceCounter(&startTicks);
    }
    float end()
    {
        QueryPerformanceCounter(&currentTicks);
        return static_cast<float>(currentTicks.QuadPart - startTicks.QuadPart) / static_cast<float>(ticksPerSecond.QuadPart);
    }
};