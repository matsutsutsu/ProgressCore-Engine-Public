#pragma once
#include "Engine/Graphics/Resource/Model.h" // Model::NodePose の定義が必要
#include <string>
#include <vector>


struct AnimatorComponent {
    // --- アニメーション状態 ---
    int   currentAnimIndex = -1;
    float currentTimer     = 0.0f;
    float playbackSpeed    = 1.0f;
    bool  isLoop           = true;
    bool  isFinished       = false;

    // イベント検出用
    float prevTimer = 0.0f; // 前フレームの時間

    // 今フレーム発生した瞬間イベント (Sound, Particle)
    std::vector<ModelResource::AnimationEvent> triggeredEvents;

    // 今フレーム有効な持続イベント (HitBox)
    std::vector<ModelResource::AnimationEvent> activeEvents;

    // --- ブレンディング（遷移）用状態 ---
    int   targetAnimIndex    = -1;   // 遷移先のアニメーション (-1なら遷移していない)
    float targetTimer        = 0.0f; // 遷移先のタイマー
    float transitionDuration = 0.0f; // 遷移にかける総時間
    float transitionTimer    = 0.0f; // 遷移開始からの経過時間

    // --- 計算用キャッシュ (メモリ割り当て削減用) ---
    // 毎フレーム new/delete しないよう、メモリを保持しておく
    std::vector<Model::NodePose> poseBufferA;
    std::vector<Model::NodePose> poseBufferB;
    std::vector<Model::NodePose> finalPoseBuffer;

    // --- 操作メソッド ---

    // アニメーション再生（blendDuration秒かけて遷移）
    // forceReset: 同じアニメーションでも最初から再生し直すか
    void Play(int index,
        bool      loop          = true,
        float     speed         = 1.0f,
        float     blendDuration = 0.2f,
        bool      forceReset    = false)
    {
        // 無効なインデックスなら何もしない
        if (index < 0) return;

        // 既にそのアニメーションを再生中で、強制リセットでなければ何もしない（継続）
        // ただし、遷移中の場合は「遷移先」と比較する
        int activeIndex = (targetAnimIndex != -1) ? targetAnimIndex : currentAnimIndex;

        if (!forceReset && activeIndex == index && !isFinished) {
            // 設定だけ更新（速度変更などへの対応）
            playbackSpeed = speed;
            isLoop        = loop;
            return;
        }

        // ブレンディングなし、または初期状態の場合
        if (blendDuration <= 0.0f || currentAnimIndex == -1) {
            currentAnimIndex = index;
            currentTimer     = 0.0f;
            isLoop           = loop;
            playbackSpeed    = speed;
            isFinished       = false;

            // 遷移情報はリセット
            targetAnimIndex = -1;
            targetTimer     = 0.0f;
            transitionTimer = 0.0f;
        }
        else {
            // ブレンディング開始
            // もし既に遷移中なら、現在の見た目（currentとtargetの途中）をcurrentとして確定させたいが、
            // 簡易実装として「今のメイン」から「新しいターゲット」への遷移を開始する

            // ケース: A再生中にBへ遷移指示
            targetAnimIndex    = index;
            targetTimer        = 0.0f;
            transitionDuration = blendDuration;
            transitionTimer    = 0.0f;

            // ループ設定等はターゲット側の設定として保存しておく必要があるが、
            // 構造を単純化するため「遷移完了後に適用」する運用とする
            // (ここでは即座に書き換えてしまう)
            isLoop        = loop;
            playbackSpeed = speed;
            isFinished    = false;
        }

        // Play時は prevTimer
        // をリセットして、「0秒目のイベント」が確実に発火するようにする
        if (forceReset || currentAnimIndex != index) {
            prevTimer =
                -0.0001f; // 0.0fジャストだと判定漏れする可能性があるので少しマイナスにしておく
        }
    }
};
