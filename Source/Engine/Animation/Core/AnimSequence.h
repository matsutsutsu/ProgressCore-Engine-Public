#pragma once
// ===========================================================================
// 概要: 1 つのアニメーションクリップの再生設定とイベント（AnimSequence）のデータ定義。
//
// 責務:
//   - 再生対象（モデル・アニメ名・長さ）、時間軸上のイベント、速度カーブ・ルートモーションカーブを保持する
//   - nlohmann::json との相互変換（シーケンサーの保存形式）
//
// 備考: ResourceManager が読み込んで保持し、全 Entity で共有する。
//       AnimationSystem が読み、AnimationSequencerWindow が編集する。
// ===========================================================================
#include <string>
#include <vector>

#include <json.hpp>
#include "SimpleMath.h"

#include "Engine/Core/Math/CurveData.h"

// ===========================================================================
// Vector3 ⇔ json 変換
//   nlohmann は ADL で「対象の型と同じ名前空間」から to_json/from_json を探す。
//   Vector3 は DirectX::SimpleMath の型なので、変換関数もこの名前空間に置かないと
//   j["offset"] = vec や get_to(vec) がコンパイルエラーになる。
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
} // namespace DirectX::SimpleMath

// ===========================================================================
// キャンセルグループ（ComboWindow / CancelWindow が許可する行動のビットマスク）
// ===========================================================================
enum class CancelGroup : uint32_t
{
    None        = 0,
    LightAttack = 1 << 0, // 0b00001 (弱攻撃)
    HeavyAttack = 1 << 1, // 0b00010 (強攻撃)
    Dodge       = 1 << 2, // 0b00100 (回避)
    Jump        = 1 << 3, // 0b01000 (ジャンプ)
    Skill       = 1 << 4  // 0b10000 (スキル)
};

// エディタに表示するキャンセルグループの名前
struct CancelFlagDef
{
    uint32_t    flag;
    const char *name;
};

// エディタ（シーケンサー・グラフ）が表示に使う一覧。CancelGroup を増やしたらここにも足す
inline const std::vector<CancelFlagDef> &GetCancelFlagDefs()
{
    static const std::vector<CancelFlagDef> defs = {
        {(uint32_t)CancelGroup::LightAttack, "弱攻撃 (Light Attack)"},
        {(uint32_t)CancelGroup::HeavyAttack, "強攻撃 (Heavy Attack)"},
        {(uint32_t)CancelGroup::Dodge, "回避 (Dodge)"},
        {(uint32_t)CancelGroup::Jump, "ジャンプ (Jump)"},
        {(uint32_t)CancelGroup::Skill, "スキル (Skill)"}};
    return defs;
}

// ===========================================================================
// アニメーションイベント
// ===========================================================================

// イベントの種別。シーケンサーのブロックの種類と 1:1 で対応する。
// int で保存するので、追加は末尾だけ（途中に挿入しない）
enum class AnimEventType : uint8_t
{
    HitBox = 0,   // 武器タグに紐づく攻撃判定（区間）
    Sound,        // 効果音の再生（点）。stringParam=AudioConfigDXTK.json のイベント名
    Effect,       // VFX プレハブの生成（点。継続放出を含むなら区間の長さだけ出す）。stringParam=プレハブのパス
    Blackboard,   // ブラックボードのフラグの ON/OFF（区間）。読む System は未実装
    SpawnHitbox,  // 空間に独立した球状の判定を生成（区間の長さが生存時間）
    Trail,        // 武器の軌跡エフェクトの ON/OFF（区間）
    ComboWindow,  // コンボの先行入力の受付区間。stringParam=受付名
    CancelWindow, // 行動キャンセルの受付区間。stringParam=受付名
    Particle,     // GPU パーティクルの発生の ON/OFF（区間。剣にまとう演出等）。stringParam=タグ

    Count
};

// 1 つのアニメーションイベント。点イベントは startTime == endTime とする。
//   パラメータは汎用の float を使い回さず、用途ごとに名前付きのフィールドで持つ
struct AnimNotifyEvent
{
    float         startTime = 0.0f;                  // 開始時刻（秒）
    float         endTime   = 0.0f;                  // 終了時刻（秒）。点イベントは startTime と同じ
    AnimEventType type      = AnimEventType::HitBox; // 種別
    std::string   stringParam;                       // 武器タグ / サウンド名 / VFX プレハブのパス / フラグ名 など

    uint32_t cancelMask = 0; // この受付区間が許可する CancelGroup のビットマスク

    // 攻撃のパラメータ（HitBox / SpawnHitbox 用）
    float damage          = 0.0f; // 攻撃力
    float knockback       = 0.0f; // ノックバックの強さ（0 で無効）
    float hitStopDuration = 0.0f; // ヒットストップ時間（秒）
    float hitStopScale    = 1.0f; // ヒットストップ中のタイムスケール（0 で完全停止）

    // 位置のパラメータ（SpawnHitbox / Effect 用）
    DirectX::SimpleMath::Vector3 offset = DirectX::SimpleMath::Vector3::Zero; // 発生位置のオフセット
    float                        radius = 1.0f;                               // 球の半径（SpawnHitbox）

    // SpawnHitbox が生成する判定球の物理レイヤー番号（誰に当てるか）。
    //   既定の 3 は PLAYER_ATTACK（ボスに当たる）、ボスの攻撃なら 4 = BOSS_ATTACK（プレイヤーに当たる）。
    //   Jolt に依存しないよう uint8_t で持ち、購読側で ObjectLayer にキャストする
    uint8_t attackLayer = 3;

    // SpawnHitbox の判定球を持ち主に追従させるか。
    //   false: 生成した瞬間のワールド位置に固定（衝撃波など）
    //   true : offset を持ち主のローカル座標として付いていき、持ち主のアニメが切り替わったら消える（突進など）
    bool hitboxFollow = false;

    // エフェクトのパラメータ（Effect 用）
    float effectScale  = 1.0f;  // 大きさの倍率（粒子の大きさ・発生範囲・速度にまとめて掛かる）
    bool  effectFollow = false; // 基準（キャラ or 武器）に追従するか

    // 基準にする武器のタグ（Effect 用。CombatRoster で引く）。空ならキャラが基準。
    //   stringParam はプレハブのパスに使っているので別に持つ
    std::string effectAttachTag;

    // 効果音のパラメータ（Sound 用）
    float soundVolume = 1.0f; // 音量の倍率（1 で等倍、0 で無音）
    float soundPitch  = 0.0f; // ピッチ（オクターブ単位。0 で等倍、±1 で 1 オクターブ上下）
    float soundPitchRandom = 0.0f; // 鳴らすたびにピッチを ±この幅でずらす（オクターブ単位。0 でずらさない）。同じ音の連続を目立たせない

    // stringParam のハッシュ（HitBox のみ。JSON には保存しない）。
    //   毎フレームのハッシュ計算を避けるため ResourceManager::LoadAnimSequence が計算する。
    //   from_json だけで読んだ場合は 0 のまま
    uint32_t tagHash = 0;
};

// ===========================================================================
// AnimSequence（1 クリップ = 1 シーケンス）
// ===========================================================================
struct AnimSequence
{
    std::string sequenceName;    // 例: "Goblin_Attack"
    std::string targetModelPath; // 例: "Assets/Models/Goblin.gltf"
    std::string targetAnimName;  // 例: "Attack"
    float       duration = 0.0f; // クリップの長さ（秒）

    // startTime の昇順であること（AnimationSystem が nextEventIndex で順に見る前提）。
    //   ResourceManager::LoadAnimSequence がロード時に並べ替える
    std::vector<AnimNotifyEvent> events;

    CurveData speedCurve;      // 再生速度カーブ（進捗 → 速度倍率）
    CurveData rootMotionCurve; // ルートモーションカーブ（進捗 → 累積前進距離 m）
};

// ===========================================================================
// nlohmann::json 変換（シーケンサーの保存形式）
// ===========================================================================
using json = nlohmann::json;

// AnimNotifyEvent ⇔ json
inline void to_json(json &j, const AnimNotifyEvent &e)
{
    j = json{
        {"startTime", e.startTime}, {"endTime", e.endTime}, {"type", (int)e.type}, {"stringParam", e.stringParam}, {"cancelMask", e.cancelMask}, {"damage", e.damage}, {"knockback", e.knockback}, {"hitStopDuration", e.hitStopDuration}, {"hitStopScale", e.hitStopScale}, {"radius", e.radius}, {"attackLayer", e.attackLayer}, {"hitboxFollow", e.hitboxFollow}, {"effectScale", e.effectScale}, {"effectFollow", e.effectFollow}, {"effectAttachTag", e.effectAttachTag}, {"soundVolume", e.soundVolume}, {"soundPitch", e.soundPitch}, {"soundPitchRandom", e.soundPitchRandom}};

    // Vector3 は初期化子リストの中では変換できない（ADL で解決されない）ので、後から代入する
    j["offset"] = e.offset;
}
inline void from_json(const json &j, AnimNotifyEvent &e)
{
    j.at("startTime").get_to(e.startTime);
    j.at("endTime").get_to(e.endTime);
    e.type            = (AnimEventType)j.value("type", 0);
    e.stringParam     = j.value("stringParam", std::string{});
    e.cancelMask      = j.value("cancelMask", 0u);
    e.damage          = j.value("damage", 0.0f);
    e.knockback       = j.value("knockback", 0.0f);
    e.hitStopDuration = j.value("hitStopDuration", 0.0f);
    e.hitStopScale    = j.value("hitStopScale", 1.0f);
    if (j.contains("offset")) j.at("offset").get_to(e.offset);
    e.radius       = j.value("radius", 1.0f);
    e.attackLayer  = static_cast<uint8_t>(j.value("attackLayer", 3));
    e.hitboxFollow = j.value("hitboxFollow", false);
    e.effectScale  = j.value("effectScale", 1.0f);
    e.effectFollow = j.value("effectFollow", false);
    e.effectAttachTag = j.value("effectAttachTag", std::string{});
    e.soundVolume  = j.value("soundVolume", 1.0f);
    e.soundPitch   = j.value("soundPitch", 0.0f);
    e.soundPitchRandom = j.value("soundPitchRandom", 0.0f);
}

// AnimSequence ⇔ json
inline void to_json(json &j, const AnimSequence &s)
{
    j = json{
        {"sequenceName", s.sequenceName}, {"targetModelPath", s.targetModelPath}, {"targetAnimName", s.targetAnimName}, {"duration", s.duration}, {"events", s.events}, {"speedCurve", s.speedCurve}, {"rootMotionCurve", s.rootMotionCurve}};
}
inline void from_json(const json &j, AnimSequence &s)
{
    s.sequenceName    = j.value("sequenceName", std::string{});
    s.targetModelPath = j.value("targetModelPath", std::string{});
    s.targetAnimName  = j.value("targetAnimName", std::string{});
    s.duration        = j.value("duration", 0.0f);
    if (j.contains("events"))
        j.at("events").get_to(s.events);
    if (j.contains("speedCurve"))
        j.at("speedCurve").get_to(s.speedCurve);
    if (j.contains("rootMotionCurve"))
        j.at("rootMotionCurve").get_to(s.rootMotionCurve);
}