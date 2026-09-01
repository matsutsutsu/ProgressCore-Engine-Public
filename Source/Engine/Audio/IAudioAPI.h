// 音声再生の抽象境界。ゲームコードはバックエンド（DirectXTK / FMOD）を一切知らない。
//
// 【イベント名はハッシュで引く】
//   ゲームコードは "sfx:/shoot" のような文字列で書けるが、実行時に毎回
//   文字列比較すると再生のたびにコストが乗る。名前は StringHash で uint32_t に潰し、
//   ソート済み配列の二分探索で引く。文字列版はファイル末尾のインラインヘルパーで、
//   ハッシュ化してハッシュ版へ委譲するだけ。
//
// 【FMOD 前提の名残がある】
//   Bank / Event という語彙は FMOD 由来。現行バックエンドは DirectXTK Audio
//   （DxtkAudioManager）で、Bank の概念が無いため LoadBank は空実装になっている。
//   インターフェースを削っていないのは FMOD へ戻す余地を残しているため。
#pragma once
#include <string>
#include <vector>
#include <DirectXMath.h>
#include "Engine/Core/Math/StringHash.h" // イベント名をハッシュ化して引くため

class IAudioAPI {
public:
    virtual ~IAudioAPI() = default;

    // ---- ライフサイクル ----------------------------------------------------

    // バックエンドを起こす。Scene 遷移ごとに1度だけ呼ぶ。
    virtual void Initialize() = 0;

    // 3D音響の再計算と、再生し終えたインスタンスの回収。毎フレーム必須。
    virtual void Update() = 0;

    // ---- データロード ------------------------------------------------------

    /**
     * @brief JSON から「イベント名 → 音源ファイル」の対応を一括ロードする。
     * @param configFilePath 実運用のパスは "Assets/Config/AudioConfigDXTK.json"（Framework が渡す）
     */
    virtual void LoadConfig(const std::string& configFilePath) = 0;

    // 音声波形の塊をメモリへ載せる。
    //
    // FMOD 時代の API。DirectXTK バックエンドでは Bank の概念が無く空実装。
    virtual void LoadBank(const char* bankFilePath) = 0;

    // イベント名を辞書へ1件登録する。通常は LoadConfig が内部で呼ぶ。
    virtual void LoadEvent(const char* eventPath) = 0;

    // 辞書をソートして検索可能な状態にする。ロード完了後に必ず1回呼ぶこと。
    //
    // これを忘れると二分探索の前提が崩れ、登録済みの音が「無い」と判定される。
    virtual void FinalizeEventLoading() = 0;

    // ---- ワンショット再生（撃ちっぱなし）------------------------------------
    // 銃声・足音・UIクリックなど、鳴らした後に触らない音。
    // 制御用IDを返さないので、後から停止もパラメータ変更もできない。

    // 距離減衰なしで再生する（UI・システム音）。
    virtual void PlayOneShot(uint32_t eventHash) = 0;

    // ワールド座標で再生する。発音位置はその瞬間に固定され、以降は追従しない。
    virtual void PlayOneShot3D(uint32_t eventHash, const DirectX::XMFLOAT3& position) = 0;

    // ---- 継続再生（後から制御する音）----------------------------------------
    // BGM・エンジン音など、停止やピッチ変更が要る音。
    // 呼び出し側は戻り値のIDを保持する責任を持つ。捨てると止められなくなる。

    /**
     * @brief 距離減衰なしで再生し、制御用IDを発行する。
     * @return 再生インスタンスのID。再生に失敗した場合は 0（＝無効値）。
     */
    virtual uint32_t PlayEvent(uint32_t eventHash) = 0;

    /**
     * @brief ワールド座標で再生し、制御用IDを発行する。座標は SetEvent3DAttributes で更新できる。
     * @return 再生インスタンスのID。再生に失敗した場合は 0（＝無効値）。
     */
    virtual uint32_t PlayEvent3D(uint32_t eventHash, const DirectX::XMFLOAT3& position) = 0;

    /**
     * @brief 指定IDの音を止める。
     * @param allowFadeOut false にすると設定されたフェード時間を無視して即座に切る。
     */
    virtual void StopEvent(uint32_t playingId, bool allowFadeOut = true) = 0;

    // バックエンドが鳴らしている音を全て即時停止し、追跡中のインスタンスを破棄する。
    //
    // シーン終了時の後始末はこちらを使う。
    // 個別の StopEvent は「DestroyTag が正しく付き playingId が生きている」前提に
    // 依存するが、こちらは前提ゼロで黙らせるため、再戦時に BGM が二重に鳴る類の
    // 取りこぼしを構造的に潰せる。
    virtual void StopAll() = 0;

    /**
     * @brief 再生中の音へ動的パラメータを送る。
     * @param paramName バックエンドが解釈する名前（"volume" / "pitch" / "pan"）
     */
    virtual void SetEventParameter(uint32_t playingId, const char* paramName, float value) = 0;

    // 継続再生中の音の発音位置を更新する。移動する音源は毎フレーム呼ぶ。
    virtual void SetEvent3DAttributes(uint32_t playingId, const DirectX::XMFLOAT3& pos) = 0;

    // ---- 3Dリスナー --------------------------------------------------------

    // 聴き手（＝カメラ）の位置と姿勢を渡す。距離減衰とパンニングの基準になる。
    virtual void SetListenerPosition(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& forward, const DirectX::XMFLOAT3& up) = 0;

    // ---- 演出フック --------------------------------------------------------

    // ワールドのスロー度を音へ反映する（0=通常速度, 1=最スロー）。
    //
    // ジャスト回避・撃破スローで全再生音のピッチを落とすために使う。
    // AudioSystem が TimeContext の時間倍率から毎フレーム駆動する。
    // 既定を空実装にしてあるので、非対応バックエンドを足しても呼び出し側は無変更でよい。
    virtual void SetGlobalSlow(float /*slow01*/) {}

    // ロード済みのイベント名一覧。インスペクタのドロップダウン用。
    //
    // 既定は空。返さないバックエンドではエディタ側が手入力へフォールバックする。
    virtual std::vector<std::string> GetEventNames() const { return {}; }

    // ---- 文字列で呼ぶためのヘルパー ----------------------------------------
    // ハッシュ版へ委譲するだけ。ホットパスでは呼び出し側でハッシュを持ち回ること。

    void PlayOneShot(const char* eventPath) { PlayOneShot(CCL::Utils::HashString(eventPath)); }
    void PlayOneShot3D(const char* eventPath, const DirectX::XMFLOAT3& position) { PlayOneShot3D(CCL::Utils::HashString(eventPath), position); }
    uint32_t PlayEvent(const char* eventPath) { return PlayEvent(CCL::Utils::HashString(eventPath)); }
};
