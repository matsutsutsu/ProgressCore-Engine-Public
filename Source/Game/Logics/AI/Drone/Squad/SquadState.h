#pragma once
// ===========================================================================
// 概要: 小隊（5機編隊）の状態と調整値、それをまとめる World リソース
//
// 備考: 書くのは SquadUpdateSystem（単スレッド）だけ。陣形・移動の並列ループからは読むだけ
//       リーダー機を持たず、先頭の位置をアンカーとして持つ。追従機が他の機体の Transform を直接引かずに済む
// ===========================================================================
#include <array>
#include <cstdint>
#include "SimpleMath.h"
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"
#include "Game/Logics/AI/Drone/Components/DroneTuning.h"

// 小隊が飛べる一番低い高さ。前の機が上へ抜けた瞬間、その真後ろの持ち場が床下にならないように使う
inline constexpr float kSquadFloorY = kDroneGroundY + 2.0f;

// 小隊の段階
enum class SquadPhase : uint8_t
{
    Idle,    // この技で飛ばない。機体は頭上円陣の席にいて、アンカーは先頭の機に重ねておくだけ
    FormFan, // ボスの頭上の扇の位置へ並び、並んだら待つ（予備動作）
    Arc,     // 撃った瞬間のプレイヤー位置へ弧を描いて飛び、そのまま通り過ぎる
    Return,  // 飛び終わって頭上円陣の席へ戻る。撃つ合図が下りるまでここに留まり、もう一度は飛ばない
};

// 小隊1個ぶんの状態。書き手はすべて SquadUpdateSystem
struct SquadState
{
    // --- アンカー（実体のない先頭） ---

    DirectX::SimpleMath::Vector3 anchorPos      = DirectX::SimpleMath::Vector3::Zero;  // 位置。席0の機がここを追う
    DirectX::SimpleMath::Vector3 anchorForward  = DirectX::SimpleMath::Vector3::UnitZ; // 向き（正規化済み）
    DirectX::SimpleMath::Vector3 anchorVelocity = DirectX::SimpleMath::Vector3::Zero;  // 速度。席0の機へ先渡しする

    SquadPhase phase = SquadPhase::Idle; // 今の段階

    DirectX::SimpleMath::Vector3 formFrom     = DirectX::SimpleMath::Vector3::Zero; // 扇へ並び始めた位置（FormFan に入った瞬間のアンカー）
    float                        formProgress = 0.0f; // 扇へ並ぶ進み具合（0=並び始め、1=並び終わった）

    // 弧の道筋。撃った瞬間に決めて、飛び終わるまで変えない（2次ベジェ2本）
    //   1本目 arcFrom → arcCorner → arcTo   … 降下しながら曲がり、プレイヤーの高さで水平に突っ込む
    //   2本目 arcTo   → passCorner → passTo … そのまま抜けて上がる
    DirectX::SimpleMath::Vector3 arcFrom     = DirectX::SimpleMath::Vector3::Zero; // 始点（撃った瞬間のアンカー）
    DirectX::SimpleMath::Vector3 arcCorner   = DirectX::SimpleMath::Vector3::Zero; // 1本目の曲がり具合を決める点
    DirectX::SimpleMath::Vector3 arcTo       = DirectX::SimpleMath::Vector3::Zero; // 終点（撃った瞬間のプレイヤー）
    DirectX::SimpleMath::Vector3 passCorner  = DirectX::SimpleMath::Vector3::Zero; // 2本目の曲がり具合を決める点
    DirectX::SimpleMath::Vector3 passTo      = DirectX::SimpleMath::Vector3::Zero; // 通り過ぎた先
    DirectX::SimpleMath::Vector3 arriveDir   = DirectX::SimpleMath::Vector3::UnitZ; // 終点へ入ってくる水平の向き。最後の機が終点を越えたかを測る
    float                        arcProgress = 0.0f; // 道筋の進み具合（0〜1=1本目、1〜2=2本目、2=道筋の先へ直進中）
    float                        coastTime   = 0.0f; // 道筋の先へ直進している時間 [秒]

    // --- 各席の機体（前フレームの値。添字は席の番号） ---

    std::array<DirectX::SimpleMath::Vector3, 5> memberPos{}; // 位置。追従機は1つ前の席のここを追う
    std::array<DirectX::SimpleMath::Vector3, 5> memberVel{}; // 速度。追従機へ先渡しする
};

// 実行中に小隊の調整 UI から変える値（しなり具合や扇の形は、動かして目で見ないと決まらないため）
struct SquadTuning
{
    // --- 隊列 ---

    float chainGap   = 2.5f;  // 前の機体との間隔 [m]
    float followGain = 10.0f; // 間隔のずれを直すバネの強さ

    // --- 扇（予備動作） ---

    int   launchCount    = 3;      // 1回の技で飛ばす小隊の数
    float fanRadius      = 14.0f;  // 扇の要から小隊までの距離 [m]
    float fanSpreadDeg   = 120.0f; // 扇の開き [度]。左端の小隊から右端の小隊までの角度
    float fanPivotHeight = 6.0f;   // 扇の要の高さ（ボス基準）[m]
    float fanFormTime    = 1.2f;   // 頭上円陣から扇の位置へ並ぶまでの秒数
    float fanHoldTime    = 1.2f;   // 並び終わってから撃つまで構える秒数。予告の長さ＝並ぶ時間＋これ

    // --- 弧（攻撃） ---

    float arcSpeed     = 40.0f; // 飛ぶ速さ [m/s]。機体の速さの上限は先渡しの速度に合わせて上がるので、上げても付いてくる
    float arcSideBulge = 8.0f;  // 扇の端の小隊ほど横へふくらませる量 [m]。左右から交差するように来る
    float passDistance = 15.0f; // プレイヤーの位置を通り過ぎてから飛ぶ水平の距離 [m]
    float passClimb    = 8.0f;  // 通り過ぎる間に上がる高さ [m]

    // --- 技の時間割 ---

    // 撃ってから頭上円陣へ戻す合図までの秒数。飛ぶ・最後の機が抜ける・席へ戻るの合計より長くする
    // 短いと飛んでいる途中で円陣へ切り替わり、弧の途中から席へ戻る
    float recoveryTime = 3.0f;
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
