#pragma once
// ===========================================================================
// 概要: 音声再生のインターフェース
//
// 責務:
//   - ゲームコードからバックエンド（今は DxtkAudioManager）を隠す
//   - ワンショット再生・続く音の制御・3D リスナー・スロー演出の窓口
//
// 備考: 音はイベント名（例: "sfx:/shoot"）を StringHash したハッシュで引き、再生のたびの文字列比較を避ける
//       文字列版はファイル末尾にあり、ハッシュにしてハッシュ版へ渡すだけ
// ===========================================================================
#include <string>
#include <vector>
#include <DirectXMath.h>
#include "Engine/Core/Math/StringHash.h"

// 音量をまとめて変える単位。イベント名が "bgm:" で始まるものが BGM、それ以外は効果音
enum class AudioCategory
{
    BGM,
    SFX,
};

class IAudioAPI
{
public:
    virtual ~IAudioAPI() = default;

    // ===========================================================================
    // ライフサイクル
    // ===========================================================================

    // バックエンドを初期化する。起動時に Framework が 1 度だけ呼ぶ
    virtual void Initialize() = 0;

    // 3D 音響の再計算と、鳴り終えた音の回収をする。1描画フレームに1回だけ呼ぶ（AudioEngineTickSystem が呼ぶ）
    // dt は追従の補間に使う経過時間（秒）
    virtual void Update(float dt) = 0;

    // ===========================================================================
    // データロード
    // ===========================================================================

    // フォルダの下（サブフォルダも含む）の音声ファイルを全部読み込み、ファイル名からイベント名を付ける
    // 名前は BGM フォルダの下なら "bgm:/ファイル名"、それ以外は "sfx:/ファイル名"（拡張子は付けない）
    // Unused という名前のフォルダの中は読まない（使わない候補の置き場）
    virtual void LoadSoundFolder(const std::string &rootDir) = 0;

    // ===========================================================================
    // ワンショット再生
    //   銃声・足音・UI クリックなど、鳴らした後に触らない音
    //   制御用 ID を返さないので、後から停止もパラメータ変更もできない
    // ===========================================================================

    // 距離減衰なしで再生する（UI・システム音）
    virtual void PlayOneShot(uint32_t eventHash) = 0;

    // 距離で減衰させずに、音量とピッチを指定して鳴らす（ムービーの効果音など）
    // volume は倍率（1 で等倍）、pitch はオクターブ（0 で等倍、±1 で1オクターブ上下）
    virtual void PlayOneShot(uint32_t eventHash, float volume, float pitch) = 0;

    // ワールド座標で鳴らす。位置は鳴らした瞬間に決まり、後から追わない
    // volume と pitch は PlayOneShot と同じ。スロー演出の分は pitch に足される
    virtual void PlayOneShot3D(uint32_t eventHash, const DirectX::XMFLOAT3 &position, float volume, float pitch) = 0;

    // 音量・ピッチを変えずにワールド座標で再生する
    void PlayOneShot3D(uint32_t eventHash, const DirectX::XMFLOAT3 &position) { PlayOneShot3D(eventHash, position, 1.0f, 0.0f); }

    // ===========================================================================
    // 継続再生（後から制御する音）
    //   BGM・環境音など、停止やパラメータ変更が要る音
    //   呼び出し側は戻り値の ID を保持すること。捨てると止められなくなる
    // ===========================================================================

    // 距離で減衰させずにループで鳴らし、制御用の ID を返す。失敗したら 0
    virtual uint32_t PlayEvent(uint32_t eventHash) = 0;

    // ワールド座標でループで鳴らし、制御用の ID を返す。失敗したら 0。位置は SetEvent3DAttributes で動かせる
    virtual uint32_t PlayEvent3D(uint32_t eventHash, const DirectX::XMFLOAT3 &position) = 0;

    // ID の音を止めて破棄する。allowFadeOut が false なら即座に切る
    // true のときの扱いはバックエンドによる（DXTK はループを抜け、今の1回を鳴らし終えて止まる）
    virtual void StopEvent(uint32_t playingId, bool allowFadeOut = true) = 0;

    // 鳴っている音をすべてすぐ止め、追っている音を破棄する。シーンの終わりの後始末に使う
    // 個別の StopEvent と違って前提がいらないので、再戦で BGM が二重に鳴るような止め漏れが起きない
    virtual void StopAll() = 0;

    // 鳴っている音へ値を送る。paramName は "volume" / "pitch" / "pan"
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

    // 世界のスローの度合い（0 が通常、1 が一番遅い）を、全部の音のピッチに反映する
    virtual void SetGlobalSlow(float /*slow01*/) {}

    // 種類ごとの音量の倍率（0 で無音、1 で等倍）。再生中の音にもすぐ効く。設定画面の音量に使う
    virtual void SetCategoryVolume(AudioCategory /*category*/, float /*volume01*/) {}

    // ロード済みのイベント名一覧（インスペクタのドロップダウン用）。空ならエディタは手入力だけになる
    virtual std::vector<std::string> GetEventNames() const { return {}; }

    // ===========================================================================
    // 文字列で呼ぶためのヘルパー
    //   ハッシュ化してハッシュ版へ渡すだけ。毎フレーム呼ぶ箇所ではハッシュを保持して使うこと
    // ===========================================================================

    void     PlayOneShot(const char *eventPath) { PlayOneShot(CCL::Utils::HashString(eventPath)); }
    void     PlayOneShot3D(const char *eventPath, const DirectX::XMFLOAT3 &position) { PlayOneShot3D(CCL::Utils::HashString(eventPath), position); }
    uint32_t PlayEvent(const char *eventPath) { return PlayEvent(CCL::Utils::HashString(eventPath)); }
};
