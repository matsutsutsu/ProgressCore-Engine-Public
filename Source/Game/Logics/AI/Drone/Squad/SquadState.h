#pragma once
// ===========================================================================
// 概要: 小隊（5機編隊）の共有状態と調整値、それをまとめる World リソース。
//
// 責務:
//   - 小隊1個ぶんの状態（SquadState）: アンカー・段階・各席の前フレームの位置と速度
//   - 実行中に触る調整値（SquadTuning）: 隊列・追跡・降下
//   - 全小隊の密配列（SquadRegistry）
//
// 備考:
//   書き手は SquadUpdateSystem（単スレッド）だけで、陣形・移動の並列ループからは読むだけ。
//
//   実体のリーダー機を持たず、先頭の位置をデータ（アンカー）として持つ。先頭機が撃破されても隊列が崩れない。
// ===========================================================================
#include <array>
#include <cstdint>
#include "SimpleMath.h"
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"
#include "Game/Logics/AI/Drone/Components/DroneTuning.h" // kDroneGroundY

// 小隊が飛べる一番低い高さ。追跡は外れても飛び続けるので、止めないと床を突き抜ける
inline constexpr float kSquadFloorY = kDroneGroundY + 2.0f;

// 小隊の段階。待機は BT が他の陣形（頭上円陣など）で作るので、ここには持たない
enum class SquadPhase : uint8_t
{
    Idle,    // 小隊突撃の指示が無い。アンカーは先頭の機に重ねておくだけ
    Descend, // 今いる場所（頭上円陣など）から真下へ降りてくる（予備動作）
    Chase,   // プレイヤーを追い続ける。攻撃の合図が落ちるまで続く
};

// 小隊1個ぶんの状態。書き手はすべて SquadUpdateSystem
struct SquadState
{
    // --- アンカー（実体の無い先頭） ---

    DirectX::SimpleMath::Vector3 anchorPos      = DirectX::SimpleMath::Vector3::Zero;  // 位置。席0の機がここを追う
    DirectX::SimpleMath::Vector3 anchorForward  = DirectX::SimpleMath::Vector3::UnitZ; // 向き（正規化済み）
    DirectX::SimpleMath::Vector3 anchorVelocity = DirectX::SimpleMath::Vector3::Zero;  // 速度。席0の機へ先渡しする

    SquadPhase phase      = SquadPhase::Idle; // 今の段階
    float      chaseSpeed = 0.0f;             // 追跡中の今の速さ [m/s]

    DirectX::SimpleMath::Vector3 descendFrom = DirectX::SimpleMath::Vector3::Zero; // 降り始めた位置（降下に入った瞬間のアンカー）
    float                        descend     = 0.0f; // 降下の進み具合（0=降り始め、1=降り切った）

    // --- 各席の機体（前フレームの値。添字は席の番号） ---

    std::array<DirectX::SimpleMath::Vector3, 5> memberPos{};   // 位置。追従機は1つ前の席のここを追う
    std::array<DirectX::SimpleMath::Vector3, 5> memberVel{};   // 速度。追従機へ先渡しする
    std::array<bool, 5>                         memberValid{}; // 上の値が有効か（撃破された席は false）

    uint8_t aliveCount = 0; // 生存機数。0 の小隊は計算をスキップ
};

// 実行中に小隊の調整 UI から変える値（しなり具合と避けやすさは、動かして目で見ないと決まらないため）
struct SquadTuning
{
    // --- 隊列 ---

    float chainGap   = 2.5f;  // 前の機体との間隔 [m]
    float followGain = 10.0f; // 間隔のずれを直すバネの強さ

    // --- 追跡 ---

    float chaseSpeedOverRun = 1.1f;   // 追跡の速さ（プレイヤーの走行速度に掛ける倍率）
    float chaseSpeedSpread  = 0.08f;  // 小隊ごとに上の倍率へ ± で足す幅
    float aimSideOffset     = 8.0f;   // 狙う点をプレイヤーの左右へずらす最大量 [m]
    float aimFadeStartDist  = 30.0f;  // 横ずれを縮め始める距離 [m]
    float aimFadeEndDist    = 12.0f;  // 横ずれが 0 になる距離 [m]。内側ではプレイヤー本人を狙う
    float turnSlowMinScale  = 0.4f;   // 大きく曲がるときに落とす速さの下限（追跡の速さに対する割合）
    float chaseAccel        = 60.0f;  // 速さを変えられる上限 [m/s²]
    float maxTurnRateDeg    = 200.0f; // 曲がれる角度の上限 [度/秒]

    // --- 降下（予備動作） ---

    float descendHeight = 5.0f; // 降り切った後の高さ（地面基準）[m]。追跡はここから始まる
    float descendTime   = 2.0f; // 降り始めてから降り切るまでの秒数。BT の予備動作の Wait をこれ以上にする
};

// 全小隊の状態をまとめた World リソース。小隊番号を添字にした密配列
struct SquadRegistry
{
    static constexpr uint8_t kMaxSquads = 32;
    static constexpr uint8_t kSquadSize = 5; // 先頭1機＋追従4機

    static constexpr uint8_t kNoSquad = 0xFF; // 無所属

    std::array<SquadState, kMaxSquads> squads{};
    SquadTuning                        tuning{};
};
