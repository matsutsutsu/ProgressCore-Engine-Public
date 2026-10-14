#pragma once
// ===========================================================================
// 概要: ムービー用カメラ（シーンに1台）の設定と再生状態を持つコンポーネント。
//
// 責務:
//   - 再生中／停止中の VirtualCamera の優先度を持つ
//   - 再生中の台本と、再生の状態・再生位置・スキップの長押しの状態を持つ
//   - トラック（SE・エフェクト・シグナル・アニメ）をどこまで出したかを持つ
//   - エディタが見せたい台本と時刻（プレビュー）を持つ
//
// 備考:
//   同じ Entity に VirtualCamera を付ける。ムービーが何本あってもこの1台で映し、
//   ムービーごとに切り替えるのは台本（どう映すかのデータ）だけにする。
// ===========================================================================
#include "Engine/Cinematic/CinematicScript.h"
#include "Engine/ECS/Common/CCL_Types.h" // CCL::ECS::EntityID

#include <cstddef>
#include <cstdint>
#include <vector>

// 再生の状態
enum class CinematicState : std::uint8_t
{
    Idle,     // 一度も再生していない
    Waiting,  // 最初の絵で止めて、開始を待っている
    Playing,  // 再生中
    Finished, // 再生が終わった（次の再生要求まで保つ）
};

// Metaファイル用の文字列配列
inline constexpr const char *kCinematicStateNames[] = {"Idle", "Waiting", "Playing", "Finished"};
inline constexpr std::size_t kCinematicStateCount   = sizeof(kCinematicStateNames) / sizeof(kCinematicStateNames[0]);

struct CinematicCameraComponent
{
    // --- 優先度 ---
    int activePriority = 300;   // 再生中のカメラの優先度。他のカメラより高くして切り替える
    int idlePriority   = -1000; // 停止中のカメラの優先度。他のカメラより低くして譲る

    // --- 実行時状態（保存しない） ---
    CinematicScript script;        // 再生中の台本。再生要求を受け付けたときに写す
    std::uint32_t   playCount = 0; // 受け付けた再生の通し番号

    CinematicState state      = CinematicState::Idle; // 再生の状態
    float          time       = 0.0f;                 // 再生位置（秒）
    float          waitTime   = 0.0f;                 // 開始待ちの経過時間（秒）
    int            activeShot = -1;                   // 今映しているショットの番号。-1 はまだ映していない

    // スキップの長押し
    float skipHoldTime = 0.0f;  // スキップボタンを押し続けている時間（秒）
    bool  skipArmed    = false; // 再生が始まってからスキップボタンを一度離したか
    bool  skipped      = false; // 今回の再生をスキップで終えたか

    // 今のショットの基準。ショットに入った瞬間に決める
    CCL::ECS::EntityID shotSubject = CCL::ECS::InvalidEntityID; // 基準の Entity。subject が空か見つからなければ Invalid
    float              shotYaw     = 0.0f;                      // 基準の向き（ラジアン、0 が +Z）

    // 入りのブレンドの補間元。ショットに入った瞬間に決める
    bool              blendActive   = false;              // 前のショットの絵から混ぜているか
    DirectX::XMFLOAT3 blendFromPos  = {0.0f, 0.0f, 0.0f}; // 補間元のカメラ位置
    DirectX::XMFLOAT3 blendFromLook = {0.0f, 0.0f, 1.0f}; // 補間元の注視点
    float             blendFromFov  = 45.0f;              // 補間元の画角（度）

    // トラックの進み具合。CinematicTrackSystem だけが書く
    std::uint32_t                   trackPlayCount = 0; // トラックが追っている再生の通し番号
    std::size_t                     nextSound      = 0; // 次に出す SE の番号（script.sounds の添字）
    std::size_t                     nextEffect     = 0; // 次に出すエフェクトの番号（script.effects の添字）
    std::size_t                     nextSignal     = 0; // 次に出すシグナルの番号（script.signals の添字）
    std::vector<CCL::ECS::EntityID> animHeld;           // アニメごとに握っている Entity（script.animations の添字）。握っていなければ Invalid
    std::vector<std::uint8_t>       animEntered;        // アニメごとに、区間に入る処理を済ませたか（script.animations の添字）

    // エディタのプレビュー。Cinematic Editor だけが書く。
    // 再生の状態（state・time・script）とは別に持つ。トラックは state を見て出すので、プレビューでは何も出ない。
    bool            previewActive = false; // エディタが、ある時刻の絵を見せているか（開始待ち・再生中は無視する）
    CinematicScript previewScript;         // 見せる台本（エディタが編集中の写し）
    float           previewTime   = 0.0f;  // 見せる時刻（秒）
};
