#pragma once
// ===================================================================================
// ファイル: AnimSequence.h
// 役割       : 1つのアニメーションの「台本」データ定義。
// 責務       : 再生対象(モデル/アニメ名/長さ) と、時間軸上のイベント(AnimNotifyEvent)、
//              および速度カーブ・ルートモーションカーブを保持する純粋データ。
// 位置付け   : マスターデータとしてメモリ常駐し、全エンティティで共有される(DOD)。
//              AnimationSystem が読み取り、AnimationSequencerWindow が編集する。
// 設計方針   : ロジックを持たない POD 寄り構造体。シリアライズは nlohmann::json を正とする
//              (Sequencer の保存形式に合わせる)。cereal も併存させ ResourceManager 経由
//              ロードに対応する。
// ===================================================================================
#include <string>
#include <vector>

#include <json.hpp>
#include "SimpleMath.h"

#include "Engine/Core/Math/CurveData.h"

// ===================================================================================
// Vector3 ⇔ json 変換
//   nlohmann の ADL は「対象型と同じ名前空間」から to_json/from_json を探す。
//   Vector3 は DirectX::SimpleMath の型なので、変換関数もこの名前空間内に置かないと
//   j["offset"] = vec / get_to(vec) の解決に失敗する(これがビルドエラーの原因)。
// ===================================================================================
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

// ===================================================================================
// キャンセルグループのビットマスク定義
// 32ビット整数の各ビットに「どの行動か」を割り当てる。
// ===================================================================================
enum class CancelGroup : uint32_t
{
    None        = 0,
    LightAttack = 1 << 0, // 0b00001 (弱攻撃)
    HeavyAttack = 1 << 1, // 0b00010 (強攻撃)
    Dodge       = 1 << 2, // 0b00100 (回避)
    Jump        = 1 << 3, // 0b01000 (ジャンプ)
    Skill       = 1 << 4  // 0b10000 (スキル)
};

// ===================================================================================
// UI表示用のメタデータとアクセサ (Single Source of Truth)
// ===================================================================================
struct CancelFlagDef
{
    uint32_t    flag;
    const char *name;
};

// インライン関数にすることで、どこから呼ばれても安全に唯一のリストを共有できる
inline const std::vector<CancelFlagDef> &GetCancelFlagDefs()
{
    static const std::vector<CancelFlagDef> defs = {
        {(uint32_t)CancelGroup::LightAttack, "弱攻撃 (Light Attack)"},
        {(uint32_t)CancelGroup::HeavyAttack, "強攻撃 (Heavy Attack)"},
        {(uint32_t)CancelGroup::Dodge, "回避 (Dodge)"},
        {(uint32_t)CancelGroup::Jump, "ジャンプ (Jump)"},
        {(uint32_t)CancelGroup::Skill, "スキル (Skill)"}
        // 新しいアクションを追加する場合は、ここにも名前を追加するだけで全UIが自動更新される！
    };
    return defs;
}

// アニメーションイベントの種別。
//
// 旧来の文字列(eventName)比較を補完する型安全な区分。
// Sequencer のUIブロック種別と1:1で対応する。
// シリアライズは int 値で行うため、末尾追加のみ可・途中挿入禁止。
enum class AnimEventType : uint8_t
{
    HitBox = 0,   // 武器タグに紐づく攻撃判定(区間)
    Sound,        // FMOD イベント再生(点)
    Effect,       // Effekseer エフェクト生成(点/区間)
    Blackboard,   // BB フラグの ON/OFF (区間 → True/False 2点に展開)
    SpawnHitbox,  // 空間に独立した球状ヒットボックスを生成(区間)
    Trail,        // 武器の軌跡エフェクト ON/OFF(区間)
    ComboWindow,  // コンボ先行入力の受付区間。stringParam=受付名
    CancelWindow, // 行動キャンセルの受付区間。stringParam=受付名
    Particle,     // GPUパーティクル発生の ON/OFF(区間)。剣にまとう演出等。stringParam=タグ

    Count
};

// 1つのアニメーションイベント通知。
//
// time(秒)で発火する。点イベントは startTime==endTime とする。
// パラメータは「用途が型から読めること」を優先し、多目的 float 流用を避けて
// 名前付きフィールドで保持する(保守性重視)。
//
// events は必ず startTime 昇順でソートされていること(AnimationSystem の
// O(1)栞ロジックの前提)。SyncToData / ロード時にソートを保証する。
struct AnimNotifyEvent
{
    float         startTime = 0.0f;                  // 発火開始時間(秒)
    float         endTime   = 0.0f;                  // 発火終了時間(秒)。点イベントは startTime と同値
    AnimEventType type      = AnimEventType::HitBox; // イベント種別
    std::string   stringParam;                       // 武器タグ / FMODパス / Effekseerパス / BBフラグ名 等

    // この窓が許可するキャンセルアクションのビットマスク
    uint32_t cancelMask = 0;

    // --- 戦闘パラメータ(HitBox / SpawnHitbox 用) ---
    float damage          = 0.0f; // 攻撃力
    float knockback       = 0.0f; // ノックバックの強さ(0で無効)。対ボスは弱め推奨。
    float hitStopDuration = 0.0f; // ヒットストップ時間(秒)
    float hitStopScale    = 1.0f; // ヒットストップ中のタイムスケール(0で完全停止)

    // --- 空間パラメータ(SpawnHitbox / Effect 用) ---
    DirectX::SimpleMath::Vector3 offset = DirectX::SimpleMath::Vector3::Zero; // 発生位置オフセット
    float                        radius = 1.0f;                               // 球半径(SpawnHitbox)

    // SpawnHitbox が生成する判定球の物理レイヤー（＝誰に当てるか）。
    //
    // PhysicsLayers の index。既定 3 = PLAYER_ATTACK(ボスに命中)。
    // ボスの攻撃なら 4 = BOSS_ATTACK(プレイヤーに命中) を選ぶ。
    // Jolt を core に引き込まないため uint8_t で保持し、購読側で ObjectLayer へキャスト。
    uint8_t attackLayer = 3;

    // --- エフェクトパラメータ(Effect 用) ---
    float effectScale  = 1.0f;  // エフェクトのスケール倍率
    bool  effectFollow = false; // 親(キャラ)に追従するか

    // --- 派生データ(ロード時にキャッシュ。JSONには保存しない) ---
    // stringParam のハッシュ値。HitBox タグ照合の毎フレーム HashString を回避する。
    //
    // ResourceManager::LoadAnimSequence が計算する。手動 from_json では 0 のまま。
    uint32_t tagHash = 0;
};

// 全エンティティで共有されるアニメーションの台本。
//
// 1アニメーション = 1シーケンス。events と2本のカーブを保持する。
struct AnimSequence
{
    std::string sequenceName;    // 例: "Goblin_Attack"
    std::string targetModelPath; // 例: "Assets/Models/Goblin.gltf"
    std::string targetAnimName;  // 例: "Attack"
    float       duration = 0.0f; // アニメーションの総時間(秒)

    // 必ず startTime 昇順でソートされていること。
    std::vector<AnimNotifyEvent> events;

    CurveData speedCurve;      // 再生速度カーブ(進捗→速度倍率)
    CurveData rootMotionCurve; // ルートモーションカーブ(進捗→累積前進距離 m)
};

// ===================================================================================
// nlohmann::json 変換(Sequencer の保存形式の正)
// ===================================================================================
using json = nlohmann::json;

// AnimNotifyEvent ⇔ json
inline void to_json(json &j, const AnimNotifyEvent &e)
{
    j = json{
        {"startTime", e.startTime}, {"endTime", e.endTime}, {"type", (int)e.type}, {"stringParam", e.stringParam}, {"cancelMask", e.cancelMask}, {"damage", e.damage}, {"knockback", e.knockback}, {"hitStopDuration", e.hitStopDuration}, {"hitStopScale", e.hitStopScale}, {"radius", e.radius}, {"attackLayer", e.attackLayer}, {"effectScale", e.effectScale}, {"effectFollow", e.effectFollow}};

    // Vector3 は初期化子リスト内で直接変換できない（ADL未解決でエラーになる）ため、
    // リスト構築後に個別代入する。これで to_json(json&, const Vector3&) が呼ばれる。
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
    e.effectScale  = j.value("effectScale", 1.0f);
    e.effectFollow = j.value("effectFollow", false);
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