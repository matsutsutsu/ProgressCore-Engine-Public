#pragma once
// ===========================================================================
// 概要: ムービーのイベントデータの構造体定義ファイル
//
// 責務:
//   - 点（時刻を過ぎた瞬間に1回出す）: SE・エフェクト・シグナル
//   - 区間（入ったら握り、出たら離す）: アニメ
//
// 備考:
//   対象（subject）は CameraShot と同じく Entity の名前で指す。
//   出すのは CinematicTrackSystem。シグナルの意味はエンジンでは決めず、ゲーム側の受け手が名前を見て解釈する。
// ===========================================================================
#include <DirectXMath.h>
#include <string>

struct AnimSequence; // 読み込んだアニメ（ResourceManager が所有）

// SE（点）
struct CinematicSound
{
    float       time = 0.0f;  // 鳴らす時刻（秒、ムービーの先頭から）
    std::string event;        // 音のイベント名
    std::string subject;      // 鳴らす位置にする Entity の名前。空なら距離で減衰しない 2D
    float       volume = 1.0f; // 音量の倍率
    float       pitch  = 0.0f; // ピッチ（オクターブ。0 で等倍）
};

// エフェクト（点）
struct CinematicEffect
{
    float             time = 0.0f;                // 出す時刻（秒、ムービーの先頭から）
    std::string       effect;                     // VFX プレハブのパス
    std::string       subject;                    // 基準にする Entity の名前
    DirectX::XMFLOAT3 offset   = {0.0f, 0.0f, 0.0f}; // 基準のローカル座標でのずれ（m）
    float             scale    = 1.0f;            // 大きさの倍率
    bool              follow   = false;           // 基準に付いていくか
    float             duration = 0.0f;            // 継続放出を出し続ける時間（秒）
};

// シグナル（点）。名前と引数をそのまま渡し、意味は受け手が決める
struct CinematicSignal
{
    float       time = 0.0f;  // 出す時刻（秒、ムービーの先頭から）
    std::string name;         // シグナルの名前
    std::string param;        // 引数1（文字列）
    std::string param2;       // 引数2（文字列）
    float       value = 0.0f; // 引数3（数値）
};

// アニメ（区間）
struct CinematicAnimation
{
    float       start    = 0.0f; // 握り始める時刻（秒、ムービーの先頭から）
    float       duration = 0.0f; // 握る長さ（秒）。読み込みで 0 ならクリップ1回分（長さ ÷ 速さ）
    std::string subject;         // 再生させる Entity の名前
    std::string sequence;        // アニメのファイルのパス
    bool        loop     = false; // ループ再生するか
    float       speed    = 1.0f;  // 再生速度の倍率
    float       blendIn  = 0.2f;  // 握るときのクロスフェード時間（秒）
    float       blendOut = 0.3f;  // 離して元のステートへ戻すときのクロスフェード時間（秒）

    const AnimSequence *resolved = nullptr; // 読み込んだアニメ（所有しない）

    float End() const { return start + duration; }
};
