#pragma once
// アニメーションの再生状態（AnimatorComponent）。再生中のクリップ・時刻・クロスフェード・ブレンドスペースを持つ
// 再生の開始は AnimControllerSystem の Play()、時刻を進めるのは AnimationSystem
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Animation/Core/AnimSequence.h"
#include "Engine/Animation/Core/AnimBlendSpaceMath.h"
#include <vector>
#include <array>
#include <cstdint>
#include <cmath>
#include <algorithm>

struct CurveData;

namespace CCL::Anim
{
    // time（秒）をクリップの長さの中に収める。ループなら先頭へ折り返し、ループしないなら末尾で止める
    //   ループしないときは負の時刻を 0 にそろえない。duration が 0 以下なら time のまま返す
    inline float WrapClipTime(float time, float duration, bool loop)
    {
        if (duration <= 0.0f) return time;
        if (!loop) return (std::min)(time, duration);

        float wrapped = std::fmod(time, duration);
        if (wrapped < 0.0f) wrapped += duration;
        return wrapped;
    }
}


// 再生中のブレンドスペース。グラフの AnimBlendSpace から、実行時に要る値だけをコピーして持つ
//   ポインタで持つと、グラフの再読込（internalGraph の差し替え）やエンティティの複製で指す先がなくなる
struct AnimBlendPlayback
{
    int                sampleCount   = 0; // 0 ならブレンドスペースではない（単体のクリップ）
    AnimBlendSpaceType type          = AnimBlendSpaceType::Directional2D;
    uint32_t           paramXHash    = 0;
    uint32_t           paramYHash    = 0;
    float              smoothingTime = 0.1f; // 入力を目標へ寄せる時間 [秒]

    // --- 点（sampleCount 個だけ有効。[0] が時間の基準）---
    std::array<const AnimSequence *, CCL::Anim::kMaxBlendSamples> sequences = {}; // 所有しない（ResourceManager が持つ）
    std::array<float, CCL::Anim::kMaxBlendSamples>                xs        = {};
    std::array<float, CCL::Anim::kMaxBlendSamples>                ys        = {};

    // --- 毎ステップの値（AnimControllerSystem が書く）---
    std::array<float, CCL::Anim::kMaxBlendSamples> weights  = {};    // 点ごとの重み。合計 1
    float                                          inputX   = 0.0f;  // 目標へ寄せた後の入力
    float                                          inputY   = 0.0f;
    bool                                           hasInput = false; // 入力を一度でも書いたか（入った直後は寄せずに目標から始める）

    bool IsActive() const { return sampleCount > 0; }

    // ブレンドスペースの定義から点をコピーし、入力と重みを初期状態に戻す。nullptr なら止める
    void Assign(const AnimBlendSpace *blendSpace)
    {
        *this = {};
        if (!blendSpace) return;

        // 上限を超えた点は使わない（エディタは上限までしか足させない）
        sampleCount   = (std::min)(static_cast<int>(blendSpace->samples.size()), CCL::Anim::kMaxBlendSamples);
        type          = blendSpace->type;
        paramXHash    = blendSpace->paramXHash;
        paramYHash    = blendSpace->paramYHash;
        smoothingTime = blendSpace->smoothingTime;
        for (int i = 0; i < sampleCount; ++i)
        {
            sequences[i] = blendSpace->samples[i].sequence;
            xs[i]        = blendSpace->samples[i].x;
            ys[i]        = blendSpace->samples[i].y;
        }

        // 最初の重みが求まるまでは、時間の基準の点だけを見せる
        if (sampleCount > 0) weights[0] = 1.0f;
    }
};

struct AnimatorComponent
{
    // --- 再生中のクリップ ---
    const AnimSequence *currentSequence       = nullptr; // 所有しない（ResourceManager が持つ）
    const CurveData    *activeCurve           = nullptr; // 再生速度カーブ。所有しない
    const CurveData    *activeRootMotionCurve = nullptr; // ルートモーションカーブ。nullptr ならボーンから抽出。所有しない

    bool lockVerticalRootMotion = false; // Force Root Lock で Y も固定するか（ステートの設定）

    float currentTimer = 0.0f; // 再生時刻（秒）

    // このステップで進める前の再生時刻（秒）。AnimationSystem が毎ステップ書く
    //   区間イベントは [previousFrameTimer, currentTimer] との重なりで判定する（飛び越し対策）
    float previousFrameTimer = 0.0f;

    float playbackSpeed = 1.0f; // ステートの再生速度。Play() が書く

    // 足滑り防止の再生速度倍率。CharacterAnimSyncSystem が毎ステップ書く
    //   playbackSpeed とまとめないこと。2 つの System が 1 つの値を書くと、
    //   遷移したステップだけステートの速度が効き、次のステップで上書きされる
    float footSlideMultiplier = 1.0f;

    bool isLoop     = true;
    bool isFinished = false; // 非ループのクリップが末尾まで再生されたか

    int nextEventIndex = 0; // 次に発火を調べるイベントの番号（events の添字）

    uint32_t playCount = 0; // Play で頭から再生し直した回数（同じクリップの再生し直しを外から見分ける）

    // --- クロスフェード（前のクリップの状態）---
    const AnimSequence *previousSequence = nullptr; // 所有しない
    float               previousTimer    = 0.0f;    // 前のクリップの再生時刻（秒）
    float               blendDuration    = 0.0f;    // クロスフェードの長さ（秒）
    float               currentBlendTime = 0.0f;    // クロスフェードの経過時間（秒）
    bool                isBlending       = false;

    // ブレンド中も前のクリップを正しく進めるため、切り替え時の設定を控えておく
    bool  previousLoop          = false;
    float previousPlaybackSpeed = 1.0f;

    // --- ブレンドスペース ---
    //   単体のクリップを再生している間は、どちらも sampleCount == 0
    //   currentSequence は、ブレンドスペースの間も時間の基準（点 [0]）のクリップを指す
    AnimBlendPlayback currentBlend;  // 今のステートのブレンドスペース
    AnimBlendPlayback previousBlend; // クロスフェード中の前のステートのブレンドスペース（切り替えた瞬間の重みのまま）

    // --- ポーズ計算用のバッファ（毎フレームの確保を避けるため保持する）---
    std::vector<Model::NodePose> poseBufferA; // 現在のクリップ（ブレンド後の結果もここ）
    std::vector<Model::NodePose> poseBufferB; // 前のクリップ
    std::vector<Model::NodePose> poseBufferC; // ブレンドスペースで、点 1 本分を計算する作業用

    // --- 外部からの握り ---
    //   エディタとムービーは別々に握って離すので、2 つのフラグで持つ
    //   1 つの値にまとめると、片方が離したときに、もう片方の握りまで消える
    bool isEditorOverride = false; // true の間はエディタ（シーケンサー）が再生を制御し、System は触らない

    // true の間は外部（タイムラインのムービー）がクリップを決める
    //   止まるのは遷移の評価・ルートモーション・足滑り補正・攻撃判定だけ
    //   時間の進行・ポーズ計算・Sound / Effect イベントは通常どおり動く
    bool isExternalOverride = false;

    // 握りを離すときに外部が立てる「今のステートへ戻して」という要求
    //   値は戻すときのクロスフェード時間（秒）。負なら要求なし。AnimControllerSystem が消費する
    float externalReleaseBlend = -1.0f;

    // Play() に渡す再生パラメータ
    //   bool と float が並ぶ引数は取り違えやすいので、指示付き初期化子で名前を書かせる（宣言順に書くこと）
    struct PlayParams
    {
        float                 blendTime              = 0.2f;    // クロスフェード時間（秒）。0 なら即座に切り替える
        bool                  loop                   = true;    // ループ再生するか
        float                 speed                  = 1.0f;    // 再生速度倍率
        bool                  lockVerticalRootMotion = false;   // Force Root Lock で Y も固定するか
        bool                  restart                = false;   // 同じクリップでも頭から再生し直すか（ステートに入るときは true）
        const AnimBlendSpace *blendSpace             = nullptr; // ブレンドスペースのステートなら、その定義（値でコピーする）。sequence には点 [0] を渡す
    };

    // 新しいシーケンスの再生を始める（blendTime があればクロスフェードする）
    void Play(const AnimSequence *sequence, const PlayParams &params = {})
    {
        // --- クリップの切り替え ---

        // 比べるのはステートではなくクリップのポインタなので、別のステートが同じクリップを指すと
        //   restart なしでは何もせず、再生位置・isFinished・イベント位置が前のステートから引き継がれる
        //   isFinished が残ると、入った直後のステップで Finished 条件が成立して抜けてしまう
        if (currentSequence != sequence || params.restart)
        {
            // 今のクリップを前のクリップとして控え、ブレンドを始める
            //   restart で同じクリップを再生し直すと previousSequence == sequence になるが、
            //   参照する時刻が違う（previousTimer と 0 秒）ので、自分自身とのクロスフェードとして成立する
            if (currentSequence && params.blendTime > 0.001f)
            {
                previousSequence = currentSequence;

                // currentTimer は duration を超えていることがあるので、クリップ内の時刻に直してから控える
                //   ループしないクリップは、末尾のポーズを保つよう末尾で止める
                previousTimer         = CCL::Anim::WrapClipTime(currentTimer, currentSequence->duration, isLoop);
                previousLoop          = isLoop;
                previousPlaybackSpeed = playbackSpeed;

                // 前のステートがブレンドスペースなら、切り替えた瞬間の重みのまま前側へ移す
                previousBlend = currentBlend;

                isBlending       = true;
                blendDuration    = params.blendTime;
                currentBlendTime = 0.0f;
            }
            else
            {
                isBlending       = false;
                previousSequence = nullptr;
                previousBlend.Assign(nullptr);
            }

            currentBlend.Assign(params.blendSpace);
            currentSequence = sequence;
            isFinished      = false;
            ++playCount;

            StartAtOffset();
        }
        else if (!params.blendSpace)
        {
            // 同じクリップを頭から流し直さずに、単体のクリップとして再生し直す要求（ムービーなど）
            //   ブレンドスペースの点 [0] と同じクリップでも、混ぜるのをやめる
            currentBlend.Assign(nullptr);
        }

        // --- 再生設定 ---
        isLoop        = params.loop;
        playbackSpeed = params.speed;

        // カーブはシーケンサーでシーケンスに書き込んだもの。空なら等速・ボーンからの抽出になる
        activeCurve           = (sequence && !sequence->speedCurve.empty()) ? &sequence->speedCurve : nullptr;
        activeRootMotionCurve = (sequence && !sequence->rootMotionCurve.empty()) ? &sequence->rootMotionCurve : nullptr;

        lockVerticalRootMotion = params.lockVerticalRootMotion;
    }

    // 再生位置を、今のクリップの開始位置（startOffset）に置く
    void StartAtOffset()
    {
        // --- 再生位置 ---

        currentTimer = 0.0f;
        if (currentSequence)
        {
            currentTimer = (std::min)((std::max)(currentSequence->startOffset, 0.0f), currentSequence->duration);
        }

        // 区間イベントは [previousFrameTimer, currentTimer] の重なりで見るので、こちらも開始位置にそろえる
        previousFrameTimer = currentTimer;

        // --- イベントの位置 ---

        // 開始位置より手前のイベントは、発火済みとして飛ばす
        //   飛ばさないと、最初のステップで手前のイベントがまとめて発火する
        //   開始位置ちょうどのイベントは飛ばさない（SyncEventCursor の <= を使わないのはそのため）
        nextEventIndex = 0;
        if (!currentSequence)
        {
            return;
        }

        const auto &events = currentSequence->events;
        while (nextEventIndex < static_cast<int>(events.size()) && events[nextEventIndex].startTime < currentTimer)
        {
            ++nextEventIndex;
        }
    }

    // nextEventIndex を今の再生時刻に合わせ直す。逆再生やシークで時刻が戻ったときに呼ぶ
    // 今より前のイベントは出したものとして扱うので、順方向に戻しても二重に出ない
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

    // エディタかムービーが再生を握っているか（グラフの遷移とルートモーションを止めるときに見る）
    bool IsOverridden() const { return isEditorOverride || isExternalOverride; }

    // クロスフェードの進み具合（0 = 前のクリップだけ、1 = 今のクリップだけ）
    float GetBlendRate() const
    {
        if (blendDuration <= 0.0f) return 1.0f;
        return std::clamp(currentBlendTime / blendDuration, 0.0f, 1.0f);
    }
};
