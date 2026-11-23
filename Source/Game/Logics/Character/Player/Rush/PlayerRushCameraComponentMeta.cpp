#include "PlayerRushCameraComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

// 1フェーズ分の構図の項目を、指定のカテゴリに並べる。4フェーズで項目と範囲をそろえる
// プレハブのキーは "locked.distance" のような入れ子の名前になる
#define RUSH_CAMERA_SHOT_FIELDS(Shot, Category)                                        \
    CFIELD_FLOAT_RANGED(Shot.distance, "距離", 0.1f, -30.0f, 15.0f, Category)          \
    CFIELD_FLOAT_RANGED(Shot.fov, "画角", 0.5f, -40.0f, 40.0f, Category)               \
    CFIELD_FLOAT_RANGED(Shot.pitch, "ピッチ", 0.5f, -45.0f, 45.0f, Category)           \
    CFIELD_FLOAT_RANGED(Shot.yaw, "ヨー", 0.5f, -90.0f, 90.0f, Category)               \
    CFIELD_FLOAT_RANGED(Shot.roll, "傾き", 0.5f, -30.0f, 30.0f, Category)              \
    CFIELD_FLOAT_RANGED(Shot.focus, "敵への注視", 0.01f, 0.0f, 1.0f, Category)         \
    CFIELD_FLOAT_RANGED(Shot.followBoost, "追従の締め", 0.5f, 0.0f, 60.0f, Category)   \
    CFIELD_FLOAT_RANGED(Shot.blendTime, "寄せきる時間", 0.01f, 0.01f, 2.0f, Category)  \
    CFIELD_FLOAT_RANGED(Shot.bounce, "行き過ぎ", 0.01f, 0.0f, 0.8f, Category)

COMPONENT_META_BEGIN_FULL(PlayerRushCameraComponent, "Player Rush Camera Component", CCL::Categories::Camera, 158, false)
RUSH_CAMERA_SHOT_FIELDS(locked, "回避離脱")
RUSH_CAMERA_SHOT_FIELDS(ready, "待機")
RUSH_CAMERA_SHOT_FIELDS(attack, "攻撃")
RUSH_CAMERA_SHOT_FIELDS(flurry, "連撃")

CFIELD_FLOAT_RANGED(releaseTime, "戻しきる時間", 0.01f, 0.01f, 3.0f, "終了・瞬間演出")
CFIELD(yawFollowsEvade, "ヨーを回避方向で反転", "終了・瞬間演出")
CFIELD_FLOAT_RANGED(startShake, "回避成立の揺れ", 0.01f, 0.0f, 1.0f, "終了・瞬間演出")
CFIELD_FLOAT_RANGED(strikeFovPunch, "斬りの画角パンチ", 0.5f, 0.0f, 40.0f, "終了・瞬間演出")
CFIELD_FLOAT_RANGED(strikeShake, "斬りの揺れ", 0.01f, 0.0f, 1.0f, "終了・瞬間演出")

CFIELD_RT(weight, "効き", "状態")
CFIELD_RT(yawSign, "ヨーの左右", "状態")
COMPONENT_META_END(PlayerRushCameraComponent)

#undef RUSH_CAMERA_SHOT_FIELDS
