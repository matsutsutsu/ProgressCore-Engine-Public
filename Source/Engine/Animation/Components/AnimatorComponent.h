#pragma once
// ===========================================================================
// 概要: アニメーションの再生状態を持つコンポーネント AnimatorComponent。
//
// 責務:
//   - 再生中の AnimSequence・再生時刻・次に発火するイベントの位置を保持する
//   - クロスフェード中の前のクリップの状態を保持する
//   - ポーズ計算用のバッファを保持する（毎フレームの確保を避けるため）
//
// 備考: 再生の開始は AnimControllerSystem が Play() で行い、時刻は AnimationSystem が進める。
// ===========================================================================
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Animation/Core/AnimSequence.h"
#include <vector>

struct CurveData;

struct AnimatorComponent
{
    // ===========================================================================
    // 再生中のクリップ
    // ===========================================================================
    const AnimSequence *currentSequence       = nullptr; // 所有しない（ResourceManager が持つ）
    const CurveData    *activeCurve           = nullptr; // 再生速度カーブ。所有しない
    const CurveData    *activeRootMotionCurve = nullptr; // ルートモーション量カーブ（進捗→累積前進距離 m）。nullptr ならボーンから抽出。所有しない

    bool lockVerticalRootMotion = false; // Force Root Lock で Y も固定するか（ステートの設定を受け取る）

    float currentTimer = 0.0f; // 再生時刻（秒）

    // このステップで進める前の再生時刻（秒）。AnimationSystem が毎ステップ書く。
    //   区間イベント（HitBox）の判定に使う。1 ステップで [start,end] を飛び越しても、
    //   [previousFrameTimer, currentTimer] と重なれば当たったと見なせる
    float previousFrameTimer = 0.0f;

    float playbackSpeed = 1.0f; // ステートの再生速度（AnimState.playbackSpeed）。Play() が設定する

    // 足滑り防止の再生速度倍率（CharacterAnimSyncSystem が毎ステップ書く）。
    //   playbackSpeed とまとめてはいけない。1 つの値を 2 つの System が書くと、ループする
    //   ステートでは遷移したステップだけステートの速度が効き、次のステップで足滑り補正に上書きされる。
    //   実効速度は AnimationSystem が playbackSpeed × footSlideMultiplier で求める
    float footSlideMultiplier = 1.0f;

    bool isLoop     = true;
    bool isFinished = false;

    // 次に発火を調べるイベントの番号。events は startTime 昇順なので、毎回先頭から探さずに済む
    int nextEventIndex = 0;

    // ===========================================================================
    // クロスフェード（前のクリップの状態）
    // ===========================================================================
    const AnimSequence *previousSequence = nullptr;
    float               previousTimer    = 0.0f;
    float               blendDuration    = 0.0f;
    float               currentBlendTime = 0.0f;
    bool                isBlending       = false;

    // ブレンド中も前のクリップを正しく進めるため、切り替え時の設定を控えておく
    bool  previousLoop          = false;
    float previousPlaybackSpeed = 1.0f;

    // ===========================================================================
    // ポーズ計算用のバッファ（毎フレームの確保を避けるため保持する）
    // ===========================================================================
    std::vector<Model::NodePose> poseBufferA; // 現在のクリップ（ブレンド後の結果もここ）
    std::vector<Model::NodePose> poseBufferB; // 前のクリップ

    bool isEditorOverride = false; // true の間はエディタ（シーケンサー）が再生を制御し、System は触らない

    // true の間は外部（タイムラインのムービー）がクリップを決める。
    //   isEditorOverride と違い、時間の進行・ポーズ計算・アニメの Sound / Effect イベントは通常どおり動く。
    //   止めるのはステートマシンの遷移評価・ルートモーション・足滑り補正・攻撃判定（HitBox / SpawnHitbox）だけ。
    //   （遷移を評価し続けると、ムービー中のクリップを Idle などへ勝手に戻してしまう）
    bool isExternalOverride = false;

    // 外部が握りを離すときに立てる、ステートマシンへの「今のステートへ戻して」という要求。
    //   値は戻すときのクロスフェード時間（秒）。負なら要求なし。AnimControllerSystem が消費する。
    //   握っている間もステートマシンのステートは変わらないので、戻す先は握る前のステートになる
    float externalReleaseBlend = -1.0f;

    // Play() に渡す再生パラメータ。
    //   bool と float が並ぶ引数は暗黙変換で取り違えやすいため、構造体と
    //   C++20 の指示付き初期化子で呼び出し側に名前を書かせる。
    //   指示付き初期化子はメンバの宣言順に書く必要がある
    struct PlayParams
    {
        float            blendTime              = 0.2f;    // クロスフェード時間（秒）。0 なら即座に切り替える
        bool             loop                   = true;    // ループ再生するか
        float            speed                  = 1.0f;    // 再生速度倍率
        const CurveData *curve                  = nullptr; // 速度カーブ（null ならシーケンス内蔵のカーブ、それも無ければ等速）
        const CurveData *rootMotionCurve        = nullptr; // ルートモーション量カーブ（null ならシーケンス内蔵、それも無ければボーンから抽出）
        bool             lockVerticalRootMotion = false;   // Force Root Lock で Y も固定するか

        // 同じクリップでも頭から再生し直すか。ステートマシンがステートに入るときは必ず true。
        //   Play が比べるのはステートではなくクリップのポインタなので、これが無いと
        //   「別ステートだが同じクリップ」で Play が何もせず、再生位置・isFinished・
        //   イベント位置・ブレンドが前のステートから引き継がれる。
        //   例: Valkyra の HitFront と HitBack は同じ Hit_Front.json を指しており、
        //       連続被弾で HitFront → HitBack に入ると isFinished が true のままなので
        //       次のステップで Finished 条件が成立し、リアクションが飛ぶ
        bool restart = false;
    };

    // 新しいシーケンスの再生を始める（blendTime があればクロスフェードする）
    void Play(const AnimSequence *sequence, const PlayParams &params = {})
    {
        if (currentSequence != sequence || params.restart)
        {
            // 今のクリップを前のクリップとして控え、ブレンドを始める。
            //   restart で同じクリップを再生し直すと previousSequence == sequence になるが、
            //   参照する時刻が違う（previousTimer と 0 秒）ので、自分自身とのクロスフェードとして成立する
            if (currentSequence && params.blendTime > 0.001f)
            {
                previousSequence = currentSequence;

                // currentTimer は duration を超えていることがあるので、クリップ内の時刻に直してから控える
                if (currentSequence->duration > 0.0f)
                {
                    if (!isLoop)
                    {
                        // 非ループ（JumpEnd 等）: 末尾のポーズを保つよう duration で止める
                        previousTimer = (std::min)(currentTimer, currentSequence->duration);
                    }
                    else
                    {
                        // ループ: はみ出た分を剰余でループ内の時刻に戻す
                        previousTimer = std::fmod(currentTimer, currentSequence->duration);
                        if (previousTimer < 0.0f)
                            previousTimer += currentSequence->duration;
                    }
                }
                else
                {
                    previousTimer = currentTimer;
                }

                previousLoop          = isLoop;
                previousPlaybackSpeed = playbackSpeed;

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
        // カーブの指定が無ければ、シーケンス自身が持つカーブ（シーケンサーで編集・保存したもの）を使う
        activeCurve = params.curve;
        if (!activeCurve && sequence && !sequence->speedCurve.empty())
            activeCurve = &sequence->speedCurve;

        activeRootMotionCurve = params.rootMotionCurve;
        if (!activeRootMotionCurve && sequence && !sequence->rootMotionCurve.empty())
            activeRootMotionCurve = &sequence->rootMotionCurve;

        lockVerticalRootMotion = params.lockVerticalRootMotion;
    }

    /**
     * @brief nextEventIndex を現在の再生時刻に合わせ直す。
     * @note 逆再生やシークで再生時刻が戻ったときに呼ぶ。現在時刻より前のイベントを
     *       発火済みとして扱うので、順方向に再開しても過去のイベントが二重に発火しない。
     */
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