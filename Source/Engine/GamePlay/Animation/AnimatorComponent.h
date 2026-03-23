#pragma once
#include "Engine/Graphics/Resource/Model.h"
#include "AnimSequence.h"
#include <vector>

struct AnimatorComponent {
    // --- 再生する台本データ ---
    const AnimSequence* currentSequence = nullptr; // 共有データへのポインタ（8バイト）

    // --- アニメーション状態（時計） ---
    float currentTimer = 0.0f;
    float playbackSpeed = 1.0f;
    bool  isLoop = true;
    bool  isFinished = false;

    // ★ O(1)アクセスのための「栞（しおり）」
    // 次にチェックするイベントのインデックス
    int nextEventIndex = 0;

    // --- 計算用キャッシュ (毎フレームのnew/deleteを防ぐマイボウル) ---
    std::vector<Model::NodePose> poseBufferA;


    void Play(const AnimSequence* sequence, bool loop = true, float speed = 1.0f) {
        // 違うアニメーションに切り替える時だけ初期化する
        if (currentSequence != sequence) {
            currentSequence = sequence;
            currentTimer = 0.0f;
            nextEventIndex = 0;
            isFinished = false;
        }
        isLoop = loop;
        playbackSpeed = speed;
    }
};