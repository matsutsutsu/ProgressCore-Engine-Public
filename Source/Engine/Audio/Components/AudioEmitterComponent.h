#pragma once
#include <cstdint>
#include <string>
#include <DirectXMath.h>

// Entity の位置から鳴り続ける音源。BGM や環境音など、ループさせる音に使う
// 再生状態は AudioSystem が書き、保存しない。撃ちっぱなしの効果音は IAudioAPI::PlayOneShot3D を使う
struct AudioEmitterComponent
{
    std::string eventName;      // 再生するイベント名（例: "bgm:/game"）。保存の基準はハッシュでなくこちら
    float       volume   = 1.0f; // 音量（1=原音）。インスペクタでは 0〜10。実行中の変更も反映される
    bool        autoPlay = true; // 生成時に自動再生するか
    bool        is3D     = true; // true=3D（距離で減衰・定位）/ false=2D（BGM 向け。位置に関係なく一様に鳴る）

    // 以下は実行時の状態（保存しない。AudioSystem が書く）
    uint32_t eventHash    = 0;     // eventName から導出したハッシュ（0 なら未算出）
    uint32_t playingId    = 0;     // IAudioAPI が発行した制御用 ID（0 は未再生）
    bool     isPlaying    = false; // 現在再生中かどうか
    uint32_t activeHash   = 0;     // 今実際に鳴らしている音のハッシュ（切り替え検知用）
    float    activeVolume = -1.0f; // 最後に適用した音量（変化検知用。負なら未適用）
    bool     activeIs3D   = true;  // 今鳴らしているインスタンスの 2D/3D（切り替え検知用）
};