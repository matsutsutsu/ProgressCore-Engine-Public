#pragma once
// ===========================================================================
// 概要: 1 つのアニメーションクリップの再生設定とイベント（AnimSequence）のデータ定義
//
// 責務:
//   - 再生するアニメ名・長さと、時間軸上のイベントを保持する
//   - 速度カーブ・ルートモーションカーブを保持する
//   - nlohmann::json との相互変換（シーケンサーの保存形式）
//
// 備考: ResourceManager が読み込んで保持し、全 Entity で共有する
//       AnimationSystem が読み、AnimationSequencerWindow が編集する
// ===========================================================================
#include <string>
#include <vector>

#include <json.hpp>
#include "SimpleMath.h"

#include "Engine/Core/Math/CurveData.h"

// ===========================================================================
// Vector3 ⇔ json 変換
//   nlohmann は ADL で、対象の型と同じ名前空間から to_json / from_json を探す
//   Vector3 は DirectX::SimpleMath の型なので、変換関数もこの名前空間に置く
//   外に置くと j["offset"] = vec や get_to(vec) がコンパイルエラーになる
// ===========================================================================
namespace DirectX::SimpleMath
{
    inline void to_json(nlohmann::json &j, const Vector3 &v)
    {
        j = nlohmann::json{{"x", v.x}, {"y", v.y}, {"z", v.z}};
    }
    inline void from_json(const nlohmann::json &j, Vector3 &v)
    {
        j.at("x").get_to(v.x);
        j.at("y").get_to(v.y);
        j.at("z").get_to(v.z);
    }
}

// ===========================================================================
// 受付区間（ComboWindow / CancelWindow）が通す遷移の種類（ビットマスク）
// ===========================================================================

// グラフの遷移の requiredCancelMask とビットが重なる受付区間の中でだけ、その遷移を通す
//   ComboWindow と CancelWindow は実行時には区別しない。どちらもこのビットだけで決まる
//   値はシーケンスとグラフのファイルに保存するので変えない
enum class CancelGroup : uint32_t
{
    None   = 0,
    Attack = 1 << 0, // 攻撃の入力（コンボの次の段・派生へ移る）
    Move   = 1 << 5  // 移動の入力（攻撃の戻りを歩き・走りで切る）
};

// エディタに表示するビットの名前
struct CancelFlagDef
{
    uint32_t    flag;
    const char *name;
};

// エディタ（シーケンサー・グラフ）が表示に使う一覧。CancelGroup を増やしたらここにも足す
inline const std::vector<CancelFlagDef> &GetCancelFlagDefs()
{
    static const std::vector<CancelFlagDef> defs = {
        {(uint32_t)CancelGroup::Attack, "攻撃 (Attack)"},
        {(uint32_t)CancelGroup::Move, "移動 (Move)"}};
    return defs;
}

// ===========================================================================
// アニメーションイベント
// ===========================================================================

// イベントの種別。シーケンサーのブロックの種類と 1:1 で対応する
//   番号を int で保存するので、番号は変えない。3 は消した種別の番号なので使わない
enum class AnimEventType : uint8_t
{
    HitBox       = 0, // 持ち主の子にある攻撃判定（HitboxComponent）を開く区間
    Sound        = 1, // 効果音の再生（点）。stringParam = サウンドのイベント名
    Effect       = 2, // VFX プレハブの生成（点）。stringParam = プレハブのパス
    SpawnHitbox  = 4, // 空間に独立した球状の判定を生成（区間の長さが生存時間）
    Trail        = 5, // 持ち主の子にある武器の軌跡の ON/OFF（区間）
    ComboWindow  = 6, // 受付区間。主にコンボの次の段へ移るのに使う
    CancelWindow = 7, // 受付区間。主に攻撃の戻りを移動で切るのに使う
    Particle     = 8, // 持ち主の子にある GPU パーティクルの発生の ON/OFF（区間）
    Finisher     = 9, // とどめの区間。この中で当てた攻撃をとどめとして扱う

    Count // 保存値の上限（欠番を含む）
};

// 1 つのアニメーションイベント。点イベントは startTime == endTime とする
//   パラメータは汎用の float を使い回さず、用途ごとに名前を付けて持つ
struct AnimNotifyEvent
{
    float         startTime = 0.0f;                  // 開始時刻（秒）
    float         endTime   = 0.0f;                  // 終了時刻（秒）。点イベントは startTime と同じ
    AnimEventType type      = AnimEventType::HitBox; // 種別
    std::string   stringParam;                       // サウンド名 / プレハブのパス（Sound / Effect）

    uint32_t cancelMask = 0; // この受付区間が通す CancelGroup のビットマスク

    // --- 攻撃（HitBox / SpawnHitbox）---
    float damage          = 0.0f; // 攻撃力
    float knockback       = 0.0f; // ノックバックの強さ（0 で無効）
    float hitStopDuration = 0.0f; // ヒットストップ時間（秒）
    float hitStopScale    = 1.0f; // ヒットストップ中のタイムスケール（0 で完全停止）

    // --- 位置（SpawnHitbox / Effect）---
    DirectX::SimpleMath::Vector3 offset = DirectX::SimpleMath::Vector3::Zero; // 発生位置のオフセット（m）
    float                        radius = 1.0f;                               // 球の半径（m）

    // 判定球の物理レイヤー番号（誰に当てるか）。3 = PLAYER_ATTACK、4 = BOSS_ATTACK
    //   Jolt に依存しないよう uint8_t で持ち、購読側で ObjectLayer にキャストする
    uint8_t attackLayer = 3;

    // 判定球を持ち主に追従させるか（SpawnHitbox）
    //   true なら持ち主のアニメが切り替わったときに消える
    bool hitboxFollow = false;

    // --- エフェクト（Effect）---
    float effectScale  = 1.0f;  // 大きさの倍率（粒子の大きさ・発生範囲・速度にまとめて掛かる）
    bool  effectFollow = false; // 基準（キャラ or 武器）に追従するか

    // 基準にする武器のタグ（CombatRoster で引く）。空ならキャラが基準
    //   stringParam はプレハブのパスに使っているので別に持つ
    std::string effectAttachTag;

    // --- 効果音（Sound）---
    float soundVolume      = 1.0f; // 音量の倍率（1 で等倍、0 で無音）
    float soundPitch       = 0.0f; // ピッチ（オクターブ。0 で等倍）
    float soundPitchRandom = 0.0f; // 鳴らすたびにずらすピッチの幅（± オクターブ。0 でずらさない）
};

// ===========================================================================
// AnimSequence（1 クリップ = 1 シーケンス）
// ===========================================================================
struct AnimSequence
{
    std::string sequenceName;    // シーケンスの名前
    std::string targetAnimName;  // モデル内のアニメーション名（再生するモデルは Entity の ModelComponent）
    float       duration = 0.0f; // クリップの長さ（秒）

    // ステートに入ったとき、クリップのこの時刻から再生を始める（秒）。0 なら頭から
    //   ループするクリップは、1周目だけここから始まり、2周目からは頭に戻る
    float startOffset = 0.0f;

    // startTime の昇順であること（AnimationSystem が nextEventIndex で順に見る前提）
    //   ResourceManager::LoadAnimSequence がロード時に並べ替える
    std::vector<AnimNotifyEvent> events;

    CurveData speedCurve;      // 再生速度カーブ（進捗 → 速度倍率）
    CurveData rootMotionCurve; // ルートモーションカーブ（進捗 → 累積前進距離 m）
};



// time（秒）が、type の区間イベントのどれかの中にあるか
//   区間の両端ちょうども中として扱う
inline bool IsInEventWindow(const AnimSequence &seq, AnimEventType type, float time)
{
    for (const auto &ev : seq.events)
    {
        if (ev.type != type) continue;
        if (time >= ev.startTime && time <= ev.endTime) return true;
    }
    return false;
}

// time（秒）が、requiredMask とビットが重なる受付区間（ComboWindow / CancelWindow）のどれかの中にあるか
//   区間の両端ちょうども中として扱う
inline bool IsInCancelWindow(const AnimSequence &seq, uint32_t requiredMask, float time)
{
    for (const auto &ev : seq.events)
    {
        if (ev.type != AnimEventType::ComboWindow && ev.type != AnimEventType::CancelWindow) continue;
        if ((ev.cancelMask & requiredMask) == 0) continue;
        if (time >= ev.startTime && time <= ev.endTime) return true;
    }
    return false;
}

// [t0, t1]（秒）に重なる type の区間イベントのうち、最初のものを返す。なければ nullptr
//   今の時刻だけで見ると、速く再生して 1 ステップで区間を丸ごと飛び越したときに取り逃す
//   t1 < t0 はループで先頭へ戻ったステップとして扱う
inline const AnimNotifyEvent *FindOverlappingEvent(const AnimSequence &seq, AnimEventType type, float t0, float t1)
{
    for (const auto &ev : seq.events)
    {
        if (ev.type != type) continue;

        // ループで戻ったステップの範囲は [t0, 末尾] と [0, t1] に分かれる
        //   どちらかに重なれば有効と見ると、末尾の時刻を使わずに判定できる
        const bool wrapped  = (t1 < t0);
        const bool overlaps = wrapped ? (ev.endTime >= t0 || ev.startTime <= t1)
                                      : (ev.startTime <= t1 && ev.endTime >= t0);
        if (overlaps) return &ev;
    }
    return nullptr;
}

// ===========================================================================
// nlohmann::json 変換（シーケンサーの保存形式）
//   キーはメンバ名のまま。ファイルにないキーは構造体の初期値で補うので、
//   後からメンバを足しても古いファイルを読める（足したメンバはここの並びにも足す）
// ===========================================================================
using json = nlohmann::json;

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AnimNotifyEvent,
                                                startTime, endTime, type, stringParam, cancelMask,
                                                damage, knockback, hitStopDuration, hitStopScale,
                                                offset, radius, attackLayer, hitboxFollow,
                                                effectScale, effectFollow, effectAttachTag,
                                                soundVolume, soundPitch, soundPitchRandom)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AnimSequence,
                                                sequenceName, targetAnimName, duration, startOffset,
                                                events, speedCurve, rootMotionCurve)
