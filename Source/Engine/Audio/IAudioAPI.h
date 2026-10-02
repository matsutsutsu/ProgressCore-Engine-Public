#pragma once
// ===========================================================================
// 概要: 音声再生の抽象インターフェース IAudioAPI。
//
// 責務:
//   - ゲームコードからバックエンド（現在は DxtkAudioManager）を隠す
//   - ワンショット再生・継続再生の制御・3D リスナー・スロー演出の窓口
//
// 備考:
//   - 音はイベント名（例: "sfx:/shoot"）で指定するが、再生のたびの文字列比較を避けるため
//     StringHash で uint32_t にしたハッシュで引く。
//     文字列版はファイル末尾のヘルパーで、ハッシュ化してハッシュ版へ渡すだけ。
//   - World リソース std::shared_ptr<IAudioAPI> として各 System から参照する。
// ===========================================================================
#include <string>
#include <vector>
#include <DirectXMath.h>
#include "Engine/Core/Math/StringHash.h" // イベント名をハッシュ化して引くため

class IAudioAPI
{
public:
    virtual ~IAudioAPI() = default;

    // ===========================================================================
    // ライフサイクル
    // ===========================================================================

    // バックエンドを初期化する。起動時に Framework が 1 度だけ呼ぶ
    virtual void Initialize() = 0;

    /**
     * @brief 3D 音響の再計算と、再生し終えたインスタンスの回収を行う。
     * @param dt このフレームの経過時間（秒）。内部の追従補間をフレーム時間に依存させないために使う
     * @note 1 描画フレームにつき必ず 1 回だけ呼ぶこと。
     *       バックエンド（DXTK の AudioEngine::Update 等）は毎フレーム 1 回を前提に
     *       ボイス管理とデバイスロスト検出を行う。FixedUpdate から呼ぶと 1 フレームに
     *       最大 4 回（Framework の MAX_STEPS）走るため、呼び出しは
     *       AudioEngineTickSystem（Render / R11_AudioTick）に限定している。
     */
    virtual void Update(float dt) = 0;

    // ===========================================================================
    // データロード
    // ===========================================================================

    /**
     * @brief JSON から「イベント名 → 音源ファイル」の対応を一括ロードする。
     * @param configFilePath 実際には "Assets/Config/AudioConfigDXTK.json"（Framework が渡す）
     */
    virtual void LoadConfig(const std::string &configFilePath) = 0;

    // ===========================================================================
    // ワンショット再生
    //   銃声・足音・UI クリックなど、鳴らした後に触らない音。
    //   制御用 ID を返さないので、後から停止もパラメータ変更もできない。
    // ===========================================================================

    // 距離減衰なしで再生する（UI・システム音）
    virtual void PlayOneShot(uint32_t eventHash) = 0;

    /**
     * @brief ワールド座標で再生する。発音位置はその瞬間に固定され、以降は追従しない。
     * @param volume 音量の倍率（1 で等倍、0 で無音）
     * @param pitch  ピッチ（オクターブ単位。0 で等倍、±1 で 1 オクターブ上下）。スロー演出の分はこれに足される
     */
    virtual void PlayOneShot3D(uint32_t eventHash, const DirectX::XMFLOAT3 &position, float volume, float pitch) = 0;

    // 音量・ピッチを変えずにワールド座標で再生する
    void PlayOneShot3D(uint32_t eventHash, const DirectX::XMFLOAT3 &position) { PlayOneShot3D(eventHash, position, 1.0f, 0.0f); }

    // ===========================================================================
    // 継続再生（後から制御する音）
    //   BGM・環境音など、停止やパラメータ変更が要る音。
    //   呼び出し側は戻り値の ID を保持すること。捨てると止められなくなる。
    // ===========================================================================

    /**
     * @brief 距離減衰なしでループ再生し、制御用 ID を発行する。
     * @return 再生インスタンスの ID。再生に失敗した場合は 0（無効値）
     */
    virtual uint32_t PlayEvent(uint32_t eventHash) = 0;

    /**
     * @brief ワールド座標でループ再生し、制御用 ID を発行する。座標は SetEvent3DAttributes で更新できる。
     * @return 再生インスタンスの ID。再生に失敗した場合は 0（無効値）
     */
    virtual uint32_t PlayEvent3D(uint32_t eventHash, const DirectX::XMFLOAT3 &position) = 0;

    /**
     * @brief 指定 ID の音を止め、インスタンスを破棄する。
     * @param allowFadeOut false なら即座に切る。true の扱いはバックエンド次第
     *                     （DXTK はループを抜け、今の再生を最後まで鳴らしてから止まる）
     */
    virtual void StopEvent(uint32_t playingId, bool allowFadeOut = true) = 0;

    /**
     * @brief バックエンドが鳴らしている音を全て即時停止し、追跡中のインスタンスを破棄する。
     * @note シーン終了時の後始末はこちらを使う。個別の StopEvent は「DestroyTag が付き、
     *       playingId が有効」という前提に依存するが、こちらは前提なしで全て止めるので、
     *       再戦時に BGM が二重に鳴るような停止漏れが起きない。
     */
    virtual void StopAll() = 0;

    /**
     * @brief 再生中の音へ動的パラメータを送る。
     * @param paramName バックエンドが解釈する名前（"volume" / "pitch" / "pan"）
     */
    virtual void SetEventParameter(uint32_t playingId, const char *paramName, float value) = 0;

    // 継続再生中の音の発音位置を更新する。移動する音源は毎フレーム呼ぶ
    virtual void SetEvent3DAttributes(uint32_t playingId, const DirectX::XMFLOAT3 &pos) = 0;

    // ===========================================================================
    // 3D リスナー
    // ===========================================================================

    // 聴き手（カメラ）の位置と姿勢を渡す。距離減衰とパンニングの基準になる
    virtual void SetListenerPosition(const DirectX::XMFLOAT3 &pos, const DirectX::XMFLOAT3 &forward, const DirectX::XMFLOAT3 &up) = 0;

    // ===========================================================================
    // 演出・エディタ支援（任意実装。既定は何もしない）
    // ===========================================================================

    /**
     * @brief ワールドのスロー度を音へ反映する（0=通常速度, 1=最スロー）。
     * @note ジャスト回避・撃破スローで全再生音のピッチを落とすために使う。
     *       AudioSystem が TimeContext の時間倍率から毎ステップ渡す。
     */
    virtual void SetGlobalSlow(float /*slow01*/) {}

    // ロード済みのイベント名一覧（インスペクタのドロップダウン用）。空ならエディタは手入力だけになる
    virtual std::vector<std::string> GetEventNames() const { return {}; }

    // ===========================================================================
    // 文字列で呼ぶためのヘルパー
    //   ハッシュ化してハッシュ版へ渡すだけ。毎フレーム呼ぶ箇所ではハッシュを保持して使うこと。
    // ===========================================================================

    void     PlayOneShot(const char *eventPath) { PlayOneShot(CCL::Utils::HashString(eventPath)); }
    void     PlayOneShot3D(const char *eventPath, const DirectX::XMFLOAT3 &position) { PlayOneShot3D(CCL::Utils::HashString(eventPath), position); }
    uint32_t PlayEvent(const char *eventPath) { return PlayEvent(CCL::Utils::HashString(eventPath)); }
};
