// CharacterMovementComponent のシリアライズおよびインスペクタGUI定義
//
// 上書き要求(overrideSpeed/use3DVelocity/jumpImpulse)と結果(currentVelocity)は
// 毎フレーム作り直される実行時状態なので、インスペクタでは読み取り専用にする。
// ここを編集可能にすると、手で入れた値が次のフレームに消えるだけでなく、
// プレハブへ実行中の値が焼き付いて「起動直後だけ速度が違う」といった事故になる。

#include "CharacterMovementComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

// ===========================================================================
// CharacterMovementComponent メタデータ定義
// BEGIN_FULL を使用しカテゴリと並び順を明記
// ===========================================================================
COMPONENT_META_BEGIN_FULL(CharacterMovementComponent, "Character Movement", CCL::Categories::Movement, 150, false)

// Vector3 は XMFLOAT3 と別型で CFIELD の型推論に載らないため、FD_ を直接使う。
FD_Float3("要求速度", "desiredVelocity", offsetof(ThisType, desiredVelocity), 0.05f, "意思", true,
          "進みたい向き。入力層またはAIが毎フレーム書く。", true),

    CFIELD_RO(overrideSpeed, "速度上書き", "上書き要求", "負値=上書きなし。読まれた時点で解除される1フレーム限りの依頼。")
        CFIELD_RO(use3DVelocity, "3D速度で駆動", "上書き要求", "重力と水平計算を無視して velocity3D をそのまま使う。")
            CFIELD_RO(jumpImpulse, "ジャンプ成立", "上書き要求", "押下ではなく「跳ぶと決まった」フレームだけ立つ。")

                CFIELD_FLOAT_RANGED(speedMultiplier, "速度倍率", 0.05f, 0.0f, 5.0f, "係数")

                    CFIELD_RO(isRootMotionActive, "ルートモーション中", "ルートモーション", nullptr)

                        FD_Float3("現在速度", "currentVelocity", offsetof(ThisType, currentVelocity), 0.05f, "結果", true,
                                  "慣性を含む実際の水平速度。書き手は物理側のみ。", true),

    COMPONENT_META_END(CharacterMovementComponent)
