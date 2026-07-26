#pragma once
#include <cstdint>
#include <string>
#include <DirectXMath.h>

/**
 * @brief 継続して再生され、エンティティの座標に追従するオーディオエミッター。
 * @note BGM、エンジン音、燃え盛る炎の環境音などに使用する。
 */
struct AudioEmitterComponent {
    std::string eventName;      ///< 再生するイベント名（例: "bgm:/game"）。人間可読の“正”。保存対象。
    float    volume    = 1.0f;  ///< 音量 [0,1]。保存対象。実行中に変えるとライブ反映される。
    bool     autoPlay  = true;  ///< 生成時に自動再生するかどうか。保存対象。

    // --- 以下は実行時の状態（保存しない） ---
    uint32_t eventHash = 0;   ///< eventName から導出したハッシュ（0なら未算出）
    uint32_t playingId = 0;   ///< IAudioAPI が発行した制御用ID (0は停止・未再生)
    bool     isPlaying = false; ///< 現在再生中かどうか
    uint32_t activeHash   = 0;    ///< 今実際に鳴らしている音のハッシュ（切り替え検知用）
    float    activeVolume = -1.0f; ///< 最後に適用した音量（変化検知用。負なら未適用）
};