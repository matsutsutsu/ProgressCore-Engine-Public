#pragma once
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Animation/Core/AnimSequence.h"
#include <vector>
// カーブデータ構造の先行宣言（後で定義します）
struct AnimationCurve;

// ===================================================================================
// ファイル: AnimatorComponent.h
// 概要: アニメーションの「再生状態」を保持するコンポーネント
// 
// [ 役割 ]
// AnimControllerSystem から渡された台本（AnimSequence）を記憶し、
// 現在の再生時間（タイマー）や、次に発火すべきイベントの栞（インデックス）を持つ。
// また、毎フレームの動的メモリ確保を避けるため、計算済みの骨格ポーズデータを
// キャッシュとして内部に保持する（DOD的最適化）。
// ===================================================================================

struct AnimatorComponent {
    // --- 再生する台本データ ---
    const AnimSequence* currentSequence = nullptr; // 共有データへのポインタ（8バイト）

    // 再生速度を動的に変化させるカーブのポインタ
    const AnimationCurve* activeCurve = nullptr;

    // --- アニメーション状態（時計） ---
    float currentTimer = 0.0f;
    float playbackSpeed = 1.0f;
    bool  isLoop = true;
    bool  isFinished = false;

    // ★ O(1)アクセスのための「栞（しおり）」
    // 次にチェックするイベントのインデックス
    int nextEventIndex = 0;

    // =========================================================
    // クロスフェード・ブレンディング用データ
    // =========================================================
    const AnimSequence* previousSequence = nullptr;
    float previousTimer = 0.0f;
    float blendDuration = 0.0f;
    float currentBlendTime = 0.0f;
    bool isBlending = false;

    // --- 計算用キャッシュ (毎フレームのnew/deleteを防ぐマイボウル) ---
    std::vector<Model::NodePose> poseBufferA;
    std::vector<Model::NodePose> poseBufferB; // 過去のアニメーション用

    // エディタからの強制制御フラグ
    bool isEditorOverride = false;

    /// @brief Play() に渡す再生パラメータ群。
    /// @details bool と float が並ぶ引数列は暗黙変換で容易に取り違えるため、
    ///          構造体＋C++20指示付き初期化子で呼び出しを自己文書化する。
    ///          メンバの宣言順（blendTime → loop → speed → curve）で初期化すること。
    struct PlayParams
    {
        float                 blendTime = 0.2f;    // クロスフェード時間（秒）。0なら即時切替
        bool                  loop      = true;    // ループ再生するか
        float                 speed     = 1.0f;    // 再生速度倍率
        const AnimationCurve *curve     = nullptr; // 速度カーブ（nullで等速）
    };

    /// @brief 新しいシーケンスの再生を開始する（必要ならクロスフェード）。
    /// @param sequence 再生する台本。現在と同一なら再生位置はリセットしない。
    /// @param params   再生パラメータ。例: Play(seq, { .loop = false, .speed = 1.5f });
    void Play(const AnimSequence *sequence, const PlayParams &params = {})
    {
        if (currentSequence != sequence)
        {
            // 前のアニメーションを「過去」として記録し、ブレンドを開始する
            if (currentSequence && params.blendTime > 0.001f)
            {
                previousSequence = currentSequence;
                previousTimer    = currentTimer;
                isBlending       = true;
                blendDuration    = params.blendTime;
                currentBlendTime = 0.0f;
            }
            else
            {
                isBlending       = false;
                previousSequence = nullptr;
            }

            currentSequence = sequence;
            currentTimer    = 0.0f;
            nextEventIndex  = 0;
            isFinished      = false;
        }
        isLoop        = params.loop;
        playbackSpeed = params.speed;
        activeCurve   = params.curve;
    }

    /// @brief イベント発火用の栞(nextEventIndex)を、現在の再生時刻に合わせて再同期する。
    /// @details 逆再生やシーク（手動での時間移動）で再生時刻が巻き戻った際に呼ぶ。
    ///          現在時刻より手前のイベントを「発火済み」として扱うため、順方向へ
    ///          再開したときに過去のイベントが二重発火しない。
    void SyncEventCursor()
    {
        nextEventIndex = 0;
        if (!currentSequence)
            return;
        const auto &events = currentSequence->events;
        while (nextEventIndex < static_cast<int>(events.size()) && events[nextEventIndex].startTime <= currentTimer)
        {
            ++nextEventIndex;
        }
    }

};