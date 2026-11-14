#pragma once
// ムービー用カメラ（シーンに1台）の設定と再生状態。同じ Entity に VirtualCamera を付ける
// ムービーが何本あってもこの1台で映し、ムービーごとに替えるのは台本だけにする
#include "Engine/Cinematic/CinematicScript.h"
#include "Engine/ECS/Common/CCL_Types.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// 再生の状態
enum class CinematicState : std::uint8_t
{
    Idle,     // 一度も再生していない
    Playing,  // 再生中
    Finished, // 再生が終わった（次の再生要求まで保つ）
};

// Meta 用の名前（CinematicState の並びと同じ順）
inline constexpr const char *kCinematicStateNames[] = {"Idle", "Playing", "Finished"};
inline constexpr std::size_t kCinematicStateCount   = sizeof(kCinematicStateNames) / sizeof(kCinematicStateNames[0]);

struct CinematicCameraComponent
{
    int activePriority = 300;   // 再生中の優先度。他のカメラより高くして画面を握る
    int idlePriority   = -1000; // 停止中の優先度。他のカメラより低くして譲る

    // --- 再生状態（保存しない） ---
    CinematicScript script;                              // 再生中の台本の写し
    CinematicState  state         = CinematicState::Idle; // 再生の状態
    float           time          = 0.0f;                 // 再生位置（秒）
    float           playStartTime = 0.0f;                 // 今回の再生を始めた位置（秒）
    std::uint32_t   playCount     = 0;                    // 受け付けた再生の通し番号
    int             activeShot    = -1;                   // 今映しているショットの番号

    // --- スキップ ---
    float skipHoldTime = 0.0f;  // 長押しの経過（秒）
    bool  skipArmed    = false; // 再生が始まってから一度ボタンを離したか
    bool  skipped      = false; // 今回の再生をスキップで終えたか

    // --- 今のショットの基準。ショットに入った瞬間に決める ---
    CCL::ECS::EntityID shotSubject = CCL::ECS::InvalidEntityID; // 基準の Entity
    float              shotYaw     = 0.0f;                      // 基準の向き（ラジアン、0 が +Z）

    // --- 被写界深度 ---
    CinematicDof dof;              // 最後に書いた被写界深度
    float        dofWeight = 0.0f; // 効き（0〜1）

    // --- トラックの進み具合。CinematicTrackSystem だけが書く ---
    std::uint32_t                   trackPlayCount = 0; // トラックが追っている再生の通し番号
    std::size_t                     nextSound      = 0; // 次に出す SE の番号
    std::size_t                     nextEffect     = 0; // 次に出すエフェクトの番号
    std::size_t                     nextSignal     = 0; // 次に出すシグナルの番号
    std::vector<CCL::ECS::EntityID> animHeld;           // アニメごとに握っている Entity
    std::vector<std::uint8_t>       animEntered;        // アニメごとに、区間に入ったか

    // --- エディタのプレビュー。Cinematic Editor だけが書く ---
    bool            previewActive = false; // ある時刻の絵を見せているか（再生中は無視する）
    CinematicScript previewScript;         // 見せる台本（編集中の写し）
    float           previewTime   = 0.0f;  // 見せる時刻（秒）
};
