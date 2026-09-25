#pragma once
// ===========================================================================
// 概要: 対象（ボス等）の周りを回り込む導入カメラ演出のパラメータと実行時状態。
//
// 責務:
//   - 軌道（角度・半径・高さ・画角の開始値と終了値）と補間カーブを持つ
//   - 再生設定（長さ・開始待ち・自動再生・スキップ可否）を持つ
//   - 再生中／待機中の VirtualCamera 優先度を持つ
//   - 再生の実行時状態を持つ（シリアライズしない）
//
// 備考: スプライン軌道(CameraDollyCart)ではなく、角度・半径・高さの3値を補間するだけの
//       パラメトリックな軌道にしている。理由は以下。
//         ・制御点の手作業配置が要らない。ボスの位置が変わっても軌道が壊れない。
//         ・「引きから寄りへ」「回り込みながら見上げる」という定番の絵が数値3つで出せる。
//         ・注視点が常に対象なので、対象が画面外へ出ることがない。
//       逆に、地形を縫うような凝った軌道は表現できない。それが要るときは Dolly を使う。
// ===========================================================================

#include "Engine/Serialization/Core/EntityLink.h"
#include <SimpleMath.h>
#include <cstddef>
#include <cstdint>

// 補間カーブ。
//
// 既定を EaseOut にしているのは、等速だと固定の監視カメラのように見えるため。
// 速く動き出して減速しながら止まると、演出として読める。
enum class CameraEaseMode : std::uint8_t
{
    Linear,    // 等速。基本使わない
    EaseIn,    // ゆっくり始まって加速
    EaseOut,   // 速く始まって減速しながら止まる（登場演出の定番）
    EaseInOut, // 両端がなめらか
};

// 並び順がそのままインスペクタの列挙値になるので、途中に挿すと
// 保存済みプレハブの値がずれる。増やすときは末尾に足すこと。
inline constexpr const char *kCameraEaseModeNames[] = {
    "Linear",
    "EaseIn",
    "EaseOut",
    "EaseInOut",
};
inline constexpr std::size_t kCameraEaseModeCount =
    sizeof(kCameraEaseModeNames) / sizeof(kCameraEaseModeNames[0]);

// 導入カメラ演出（対象の周りを回り込む）のボディコンポーネント。
struct CameraBodyIntroOrbit
{
    // --- 対象 ---
    // 回り込む中心（ボス）。空なら CameraTargetBindSystem が
    // BossPhaseComponent を持つエンティティへ役割で自動接続する。
    CCL::Serialization::EntityLink target;
    // 注視点のオフセット。足元ではなく胸〜頭を見るため既定で上へ持ち上げる。
    DirectX::SimpleMath::Vector3 targetOffset = {0.0f, 3.0f, 0.0f};

    // --- 軌道（開始→終了へ補間） ---
    // 角度は TPS カメラと同じ規約（X軸=cos, Z軸=sin）。
    // 最短回転にはせず指定値をそのまま補間するので、200→20 と書けば
    // 「180度ぐるっと回り込む」という意図がそのまま出る。
    float startAngle  = 200.0f; // 開始の水平角(度)
    float endAngle    = 20.0f;  // 終了の水平角(度)
    float startRadius = 30.0f;  // 開始の水平距離(m)。大きいほど引きの絵から始まる
    float endRadius   = 10.0f;  // 終了の水平距離(m)
    float startHeight = 2.0f;   // 開始の高さ(m)。注視点からの相対。低いと見上げる絵になる
    float endHeight   = 6.0f;   // 終了の高さ(m)
    float startFov    = 55.0f;  // 開始の画角(度)。広いほど空間の広さが出る
    float endFov      = 35.0f;  // 終了の画角(度)。狭いほど圧が出る（望遠の圧縮効果）

    CameraEaseMode easeMode = CameraEaseMode::EaseOut; // 進行度に掛ける補間カーブ

    // --- 再生 ---
    float duration    = 4.0f; // 演出の長さ(秒)
    float startDelay  = 0.3f; // シーン開始から再生を始めるまでの待ち(秒)
    bool  playOnStart = true; // シーン開始時に自動再生するか
    bool  allowSkip   = true; // 攻撃/ジャンプ入力でスキップできるか

    // 再生中の VirtualCamera.priority。プレイヤー追従カメラ（PlayerFollowCamera.json は 200）より高くする。
    int activePriority = 300;
    // 非再生中の priority。他のどのカメラよりも低くして、確実に譲る。
    int idlePriority = -1000;

    // --- 実行時状態（シリアライズしない） ---
    bool  playing    = false; // 再生中か
    bool  finished   = false; // 再生が終わったか（中止も含む）
    bool  skipped    = false; // スキップで終わったか（終了イベントに載せる）
    float elapsed    = 0.0f;  // 再生開始からの経過時間(秒)
    float delayTimer = 0.0f;  // startDelay の消化時間(秒)
    // 対象を探し続けている時間(秒)。
    // プレハブ生成は ScrutinyAndApply まで遅延されるので、シーン開始直後は
    // ボスがまだ存在しない。1フレーム見つからないだけで打ち切ると
    // 演出が毎回スキップされるため、少し待ってから諦める。
    float resolveTimer  = 0.0f;
    bool  autoPlayArmed = true; // playOnStart の自動再生をまだ始めていないか
    // インスペクタのスクラブで絵作り中か（エディタ専用の一時状態）。
    // 再生していなくてもカメラを掌握させ、任意の進行度の絵を見えるようにする。
    bool previewing = false;
};
