#pragma once
// ===========================================================================
// 概要: タイムライン（ムービー演出）を再生するコンポーネント。
//
// 責務:
//   - 再生する資産のパスと再生設定（自動再生・開始待ち・スキップ可否）を持つ
//   - 再生中／待機中の VirtualCamera 優先度を持つ
//   - 再生位置・点イベントの発火位置・役割の結び付け・カメラ評価の実行時状態を持つ（シリアライズしない）
//
// 備考:
//   同じ Entity に VirtualCamera と TransformComponent を付ける。ムービー中のカメラは
//   この1台だけで、ショットの切り替えはこの1台の姿勢を飛ばして表現する。
//   カメラを複数台並べて優先度で切り替えると、CameraBrainSystem が切り替えのたびに
//   ブレンドを挟むため、カットがカットにならない。
// ===========================================================================
#include "Engine/ECS/Common/CCL_Types.h"
#include "Engine/Timeline/TimelineAsset.h"

#include <SimpleMath.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// 再生の状態
enum class TimelinePlayState : std::uint8_t
{
    Idle,     // 再生していない（自動再生の前、または再生要求待ち）
    Waiting,  // 開始待ち（startDelay の消化と、役割の結び付けを待っている）
    Playing,  // 再生中
    Finished, // 終わった（スキップ・中止を含む）
};

inline constexpr const char *kTimelinePlayStateNames[] = {"Idle", "Waiting", "Playing", "Finished"};
inline constexpr std::size_t kTimelinePlayStateCount =
    sizeof(kTimelinePlayStateNames) / sizeof(kTimelinePlayStateNames[0]);

// カメラが出す姿勢（位置・注視点・画角）
struct TimelineCameraPose
{
    DirectX::SimpleMath::Vector3 eye;
    DirectX::SimpleMath::Vector3 look;
    float                        fov = 45.0f;
};

// ショット開始時に固定した、基準の位置と向き
struct TimelineShotFrame
{
    DirectX::SimpleMath::Vector3 subjectPos; // 構図の基準の位置（followSubject=false のとき使う）
    DirectX::SimpleMath::Vector3 lookPos;    // 注視点の基準の位置（同上）
    float                        yaw = 0.0f; // 基準の向き（ラジアン、atan2(x, z)。0 が +Z）
};

struct TimelinePlayerComponent
{
    // --- 資産 ---
    std::string assetPath; // タイムライン JSON のパス

    // --- 再生 ---
    float startDelay  = 0.2f; // 再生要求から再生を始めるまでの待ち(秒)
    bool  playOnStart = true; // シーン開始時に自動再生するか
    bool  allowSkip   = true; // 攻撃/ジャンプ入力でスキップできるか

    // 再生中の VirtualCamera.priority。プレイヤー追従カメラ（PlayerFollowCamera.json は 200）より高くする。
    int activePriority = 300;
    // 非再生中の priority。他のどのカメラよりも低くして、確実に譲る。
    int idlePriority = -1000;

    // --- 実行時状態（シリアライズしない） ---
    std::shared_ptr<const TimelineAsset> asset;                // 読み込んだ資産
    bool                                 hasLoadError = false; // 立っていれば再読み込みしない（毎フレームのエラーログ防止）

    TimelinePlayState state         = TimelinePlayState::Idle;
    bool              autoPlayArmed = true;  // playOnStart の自動再生をまだ始めていないか
    bool              skipped       = false; // スキップで終わったか
    // インスペクタのスクラブで絵作り中か（エディタ専用の一時状態）。
    // 立っている間は再生位置を自動で進めず、time の絵をそのまま出す。
    bool previewing = false;

    // 再生位置(秒)。ロジックの固定ステップで進める。
    //   描画側は prevTime と time を RenderInterpState.alpha で補間して評価する。
    //   固定ステップの位置をそのまま使うと、描画フレームとずれてカメラがカクつく。
    float time     = 0.0f;
    float prevTime = 0.0f;

    // 点イベントの発火カーソル。次に発火するクリップの番号（asset の sounds / effects の添字）。
    //   「prevTime〜time の間にあるもの」で判定すると、0 秒のクリップや終端ちょうどのクリップを
    //   取りこぼしやすい。先頭から順に進めるカーソルなら、どの時刻のものも必ず1回だけ発火する。
    std::size_t nextSound  = 0;
    std::size_t nextEffect = 0;
    std::size_t nextSignal = 0;

    // Animation クリップごとに、いま握っている実体（asset の animations と同じ添字）。
    //   握っていなければ InvalidEntityID。離すときは役割を引き直さず、握った相手をそのまま離す
    //   （再生中に役割の結び付けが替わっても、握りっぱなしの実体を残さないため）。
    //   RequestPlay では消さない。最初から再生し直したときに、握ったままの実体を離せなくなる
    std::vector<CCL::ECS::EntityID> animHeld;

    float delayTimer   = 0.0f; // startDelay の消化時間(秒)
    float resolveTimer = 0.0f; // 役割の結び付けを待っている時間(秒)

    // 役割名 → 実体。ゲーム側の TimelineRoleBindSystem が書く
    std::unordered_map<std::string, CCL::ECS::EntityID> bindings;

    // --- カメラ評価の実行時状態（TimelineCameraSystem が書く） ---
    int                activeShot = -1;     // いま評価しているショットの番号。-1 は未評価
    TimelineShotFrame  shotFrame;           // activeShot の開始時に固定した基準
    TimelineCameraPose lastPose;            // 前フレームに出した姿勢（Blend の補間元）
    TimelineCameraPose blendFrom;           // いまのショットの Blend の補間元
    bool               hasLastPose = false; // lastPose が有効か
    bool               hasBlend    = false; // blendFrom が有効か

    /**
     * @brief 最初から再生し直す。startDelay と役割の結び付けを待ってから再生が始まる。
     *
     * 再生要求イベントとインスペクタのボタンの両方から呼ぶので、ここに1つにまとめる。
     * 片方だけ初期化し忘れると、2回目の再生だけ前回のショットから Blend してしまう。
     */
    void RequestPlay()
    {
        state         = TimelinePlayState::Waiting;
        autoPlayArmed = false;
        skipped       = false;
        previewing    = false;
        time          = 0.0f;
        prevTime      = 0.0f;
        nextSound     = 0;
        nextEffect    = 0;
        nextSignal    = 0;
        delayTimer    = 0.0f;
        resolveTimer  = 0.0f;
        activeShot    = -1;
        hasLastPose   = false;
        hasBlend      = false;
    }
};
