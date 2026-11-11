#include "CharacterMovementComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

COMPONENT_META_BEGIN_FULL(CharacterMovementComponent, "Character Movement", CCL::Categories::Movement, 150, false)

// Vector3 は CFIELD の型推論に載らないので、FD_ を直接使う
FD_Float3("要求速度", "desiredVelocity", offsetof(ThisType, desiredVelocity), 0.05f, "意思", true,
          "進みたい向き。入力層またはAIが毎フレーム書く。", true),
CFIELD_RO(moveIntensity, "移動の強さ", "意思", "アナログ傾き量(0〜1)。通常歩行の速度割合。既定1.0=全開。毎フレーム入力層が書く。")
CFIELD_RO(overrideSpeed, "速度上書き", "上書き要求", "負値=上書きなし。読まれた時点で解除される1フレーム限りの依頼。")
CFIELD_RO(use3DVelocity, "3D速度で駆動", "上書き要求", "重力と水平計算を無視して velocity3D をそのまま使う。")
CFIELD_RO(jumpImpulse, "ジャンプ成立", "上書き要求", "押下ではなく「跳ぶと決まった」フレームだけ立つ。")

CFIELD_FLOAT_RANGED(speedMultiplier, "速度倍率", 0.05f, 0.0f, 5.0f, "係数")

CFIELD_RO(isRootMotionActive, "ルートモーション中", "ルートモーション", nullptr)

FD_Float3("現在速度", "currentVelocity", offsetof(ThisType, currentVelocity), 0.05f, "結果", true,
                                  "慣性を含む、物理へ渡した水平速度（壁に当たる前）。書き手は物理側のみ。", true),

COMPONENT_META_END(CharacterMovementComponent)
