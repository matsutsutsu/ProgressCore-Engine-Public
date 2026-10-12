#pragma once
// ===========================================================================
// 概要: ムービー演出の台本（タイムライン）のデータ定義と、JSON からの読み込み。
//
// 責務:
//   - トラックとクリップのデータを持つ（Camera / Sound / Effect / Animation / Signal）
//   - JSON（Assets/Timelines/*.json）を読み込んでデータへ変換する
//   - クリップが参照する役割名（"Player" / "Boss" など）を列挙する
//
// 備考:
//   再生は TimelineSystem（再生位置を進める）、カメラの評価は TimelineCameraSystem が行う。
//   ここはデータと読み込みだけを持つ。
//
//   クリップは対象をエンティティ ID ではなく役割名で指す。ボスやプレイヤーは実行時に
//   プレハブから生成されるので、ID を資産に書いておくことはできない。役割名と実体の
//   対応は、ゲーム側の System（TimelineRoleBindSystem）が再生時に結び付ける。
// ===========================================================================
#include <SimpleMath.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct AnimSequence; // AnimationClip が解決結果を指す（Engine/Animation/Core/AnimSequence.h）

// 補間カーブ。進行度（0〜1）に掛ける。
enum class TimelineEase : std::uint8_t
{
    Linear,    // 等速
    EaseIn,    // ゆっくり始まって加速
    EaseOut,   // 速く始まって減速しながら止まる
    EaseInOut, // 両端がなめらか
};

// カメラの動かし方。
enum class CameraShotMotion : std::uint8_t
{
    Truck, // 開始位置から終了位置へ平行に動く（横移動・前進など）
    Orbit, // 注視点の周りを、角度・距離・高さを補間して回り込む
};

// オフセットを測る座標系。
enum class CameraShotSpace : std::uint8_t
{
    World,        // ワールド軸。対象の向きに関係なく同じ方向へずらす
    SubjectLocal, // 対象の向き基準（+X=右, +Y=上, +Z=前）。ショット開始時の向きで固定する
};

// 前のショットからのつなぎ方。
enum class CameraShotTransition : std::uint8_t
{
    Cut,   // 瞬時に切り替える
    Blend, // 前のショットの最後の姿勢から blendTime 秒かけて補間する
};

/**
 * @brief カメラトラックの1ショット。
 *
 * 位置はすべて「対象からのオフセット」で書く。
 * ワールド座標で書くと、スポーン位置やボスの配置が変わるたびに構図が崩れる。
 */
struct CameraShotClip
{
    // JSON の "_comment"。読み込みでは使わず、エディタで保存し直しても消えないように持つ
    std::string comment;

    float start    = 0.0f; // 開始時刻（秒、タイムラインの先頭から）
    float duration = 2.0f; // 長さ（秒）

    // 構図の基準にする役割名。空なら原点・ワールド軸（オフセットがそのままワールド座標になる）
    std::string subject;
    // 注視点の基準にする役割名。空なら subject と同じ（プレイヤー越しにボスを見る、などで使う）
    std::string lookSubject;

    CameraShotSpace space = CameraShotSpace::SubjectLocal;
    // true なら対象が動いても毎フレーム追う。false ならショット開始時の位置に固定する
    bool followSubject = false;

    CameraShotMotion motion = CameraShotMotion::Truck;

    // 注視点（Truck / Orbit 共通）。基準からのオフセット（m）
    DirectX::SimpleMath::Vector3 lookStart = {0.0f, 1.5f, 0.0f};
    DirectX::SimpleMath::Vector3 lookEnd   = {0.0f, 1.5f, 0.0f};

    // Truck 用: カメラ位置。基準からのオフセット（m）
    DirectX::SimpleMath::Vector3 eyeStart = {-3.0f, 1.5f, 4.0f};
    DirectX::SimpleMath::Vector3 eyeEnd   = {3.0f, 1.5f, 4.0f};

    // Orbit 用: 注視点を中心にした水平角（度）・水平距離（m）・高さ（m）。
    // 角度は TPS カメラと同じ規約（X軸=cos, Z軸=sin）。
    //
    // 最短回転にはせず指定値をそのまま補間する。
    // 90→-90 と書けば「180度回り込む」という意図がそのまま出る。
    float angleStart  = 0.0f;
    float angleEnd    = 90.0f;
    float radiusStart = 10.0f;
    float radiusEnd   = 10.0f;
    float heightStart = 2.0f;
    float heightEnd   = 2.0f;

    float fovStart = 45.0f; // 開始の画角（度）
    float fovEnd   = 45.0f; // 終了の画角（度）

    TimelineEase ease = TimelineEase::EaseInOut; // 進行度に掛ける補間カーブ

    CameraShotTransition transitionIn = CameraShotTransition::Cut; // 前のショットからのつなぎ方
    float                blendTime    = 0.5f;                      // Blend のときの補間時間（秒）

    float End() const { return start + duration; }
};

// サウンドトラックの1クリップ（点）。再生位置が time を越えた瞬間に1回鳴らす。
struct SoundClip
{
    // JSON の "_comment"。読み込みでは使わず、エディタで保存し直しても消えないように持つ
    std::string comment;

    float       time = 0.0f; // 鳴らす時刻（秒）
    std::string event;       // AudioConfigDXTK.json に登録したイベント名

    // true なら subject の位置で 3D に鳴らす（距離で減衰し、左右に振れる）。
    // false なら距離減衰なしの 2D。ムービーはカメラが対象から遠いことが多いので既定は 2D。
    bool        spatial = false;
    std::string subject; // spatial のときの発音位置にする役割名

    float volume = 1.0f; // 音量の倍率（1 で等倍）
    float pitch  = 0.0f; // ピッチ（オクターブ単位。0 で等倍）

    // スキップで飛ばされたときも発火するか。音は演出なので既定は捨てる
    bool fireOnSkip = false;
};

/**
 * @brief エフェクトトラックの1クリップ。再生位置が time を越えた瞬間に VFX を出す。
 *
 * VFX の生成はアニメの Effect イベントと同じ経路（AnimEventEffectMessage → VFXSystem）を通す。
 * そのため offset・scale・follow・attachTag の意味もアニメのシーケンサーと同じ。
 */
struct EffectClip
{
    // JSON の "_comment"。読み込みでは使わず、エディタで保存し直しても消えないように持つ
    std::string comment;

    float time     = 0.0f; // 出す時刻（秒）
    float duration = 0.0f; // 継続放出を出し続ける時間（秒）。バーストだけの VFX では使わない

    std::string effect; // VFX プレハブのパス（例: "Assets/Prefabs/Particles/VFX_HitSpark"）

    // 基準にする役割名。空なら offset をワールド座標として出す（この場合 scale/follow は効かない）
    std::string subject;
    std::string attachTag; // 基準にする武器のタグ（subject の CombatRoster で引く）。空なら subject が基準

    DirectX::SimpleMath::Vector3 offset = DirectX::SimpleMath::Vector3::Zero; // 基準のローカル座標でのずれ（m）
    float                        scale  = 1.0f;                               // 大きさの倍率
    bool                         follow = false;                              // 基準に追従するか

    // スキップで飛ばされたときも発火するか。見た目の演出なので既定は捨てる
    bool fireOnSkip = false;
};

/**
 * @brief アニメーショントラックの1クリップ（区間）。
 *
 * 区間の間だけ subject の AnimatorComponent を握り（isExternalOverride）、指定のクリップを再生する。
 *
 * 区間が終わったら握りを離し、ステートマシンが元のステートのクリップへブレンドして戻す。
 */
struct AnimationClip
{
    // JSON の "_comment"。読み込みでは使わず、エディタで保存し直しても消えないように持つ
    std::string comment;

    float start = 0.0f; // 再生を始める時刻（秒）
    // 握っている長さ（秒）。0 ならクリップの長さ÷速度（読み込み時に埋める。loop のときは要指定）
    float duration = 0.0f;

    std::string subject;  // 再生させる役割名
    std::string sequence; // AnimSequence の JSON（例: "Assets/Animations/Sequencer/Muriel/Roar.json"）

    bool  loop     = false; // ループ再生するか（待機ポーズを延ばすときなど）
    float speed    = 1.0f;  // 再生速度の倍率
    float blendIn  = 0.2f;  // 握る直前のクリップからのクロスフェード時間（秒）
    float blendOut = 0.3f;  // 離すときに元のステートへ戻すクロスフェード時間（秒）

    // 読み込み時に TimelineSystem が解決する（ResourceManager が所有。JSON には書かない）
    const AnimSequence *resolved = nullptr;

    float End() const { return start + duration; }
};

/**
 * @brief シグナルトラックの1クリップ（点）。再生位置が time を越えた瞬間に TimelineSignalEvent を発行する。
 *
 * タイムラインはシグナルの意味を知らない。名前と引数をそのまま渡し、ゲーム側の受け手が解釈する
 * （テロップ・BGM の切り替え・戦闘開始など）。
 *
 * 受け手ごとの引数の使い方は受け手の側に書く。
 */
struct SignalClip
{
    // JSON の "_comment"。読み込みでは使わず、エディタで保存し直しても消えないように持つ
    std::string comment;

    float       time = 0.0f; // 発行する時刻（秒）
    std::string name;        // シグナル名（例: "Telop" / "BGM"）
    std::string param;       // 引数1（文字列）
    std::string param2;      // 引数2（文字列）
    float       value = 0.0f; // 引数3（数値）

    // スキップで飛ばされたときも発行するか。
    // シグナルはゲームの状態を変えるもの（BGM・戦闘開始）に使うことが多く、飛ばすと本編が壊れるので既定は発行する。
    // テロップのような見た目だけのものは false にする。
    bool fireOnSkip = true;
};

// タイムライン資産の本体。
struct TimelineAsset
{
    std::string path;    // 読み込み元（ログ表示用）
    std::string comment; // JSON の "_comment"（保存し直しても消えないように持つ）

    // 全体の長さ（秒）。JSON で省略するとクリップの終端の最大値になる
    float duration = 0.0f;

    // duration を JSON で明示したか。明示していなければ、アニメの長さが決まった後に延ばしてよい
    bool durationExplicit = false;

    // カメラトラックのショット。start の昇順に並べてある
    std::vector<CameraShotClip> cameraShots;

    // 点イベントのクリップ。time の昇順に並べてある（発火は先頭から順に進めるカーソルで行う）
    std::vector<SoundClip>  sounds;
    std::vector<EffectClip> effects;
    std::vector<SignalClip> signals;

    // 区間のクリップ。start の昇順に並べてある
    std::vector<AnimationClip> animations;

    /**
     * @brief クリップが参照している役割名を重複なしで返す。
     *
     * 再生を始める前に、ここに挙がった役割がすべて実体に結び付いているかを確かめる。
     * 1つでも欠けたまま再生すると、そのショットだけカメラが原点を向く。
     */
    std::vector<std::string> CollectRoles() const;
};

namespace TimelineAssetLoader
{
    /**
     * @brief JSON ファイルからタイムラインを読み込む。
     * @param path   読み込むファイル（Assets からの相対パス）。
     * @param out    読み込み先。失敗しても途中まで書かれていることがある。
     * @param errMsg 失敗したときの理由。
     * @return 読み込めたら true。
     */
    bool Load(const std::string &path, TimelineAsset &out, std::string &errMsg);

    /**
     * @brief タイムラインを JSON ファイルへ書き出す。Load で読める形と同じ。
     * @param path   書き出すファイル（Assets からの相対パス）。
     * @param asset  書き出す内容。並べ替えはしない（呼び出し側で Normalize しておく）。
     * @param errMsg 失敗したときの理由。
     * @return 書けたら true。
     */
    bool Save(const std::string &path, const TimelineAsset &asset, std::string &errMsg);

    /**
     * @brief クリップを時刻の昇順に並べ直し、長さ（duration）を明示していなければ終端から計算し直す。
     *
     * 再生側（ショットの検索・点イベントのカーソル）は時刻の昇順を前提にしている。
     * エディタでクリップを動かしたら、再生へ渡す前に必ず通すこと。
     */
    void Normalize(TimelineAsset &asset);

    /**
     * @brief Animation クリップの AnimSequence を引き、長さ 0 のクリップをクリップの長さ÷速度で埋める。
     * @note ResourceManager を使うので、並列の ForEach の中から呼ばないこと。埋めた後は Normalize で長さを直す。
     */
    void ResolveAnimations(TimelineAsset &asset);

    // 進行度（0〜1）へ補間カーブを当てる
    float ApplyEase(float t, TimelineEase ease);
} // namespace TimelineAssetLoader

// 列挙値の名前。JSON にはこの文字列で書き、エディタのコンボにもそのまま出す。
// 並び順が列挙値そのものなので、増やすときは末尾に足すこと。
inline constexpr const char *kTimelineEaseNames[] = {"Linear", "EaseIn", "EaseOut", "EaseInOut"};
inline constexpr std::size_t kTimelineEaseCount   = sizeof(kTimelineEaseNames) / sizeof(kTimelineEaseNames[0]);

inline constexpr const char *kCameraShotMotionNames[]     = {"Truck", "Orbit"};
inline constexpr const char *kCameraShotSpaceNames[]      = {"World", "SubjectLocal"};
inline constexpr const char *kCameraShotTransitionNames[] = {"Cut", "Blend"};
