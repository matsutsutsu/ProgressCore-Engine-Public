#pragma once
// ゲーム側がカメラへ出す演出の要求（揺れ・画角パンチ・導入演出）
// カメラは要求の理由を知らない。ゲーム側が汎用の要求に直して出すので、Engine→Game の依存ができない
#include "Engine/ECS/Common/CCL_Types.h"

// 画面を揺らす要求。着地・被弾・命中など発生源は問わない
//
// 伝えるのは「どれくらいの衝撃だったか」だけ。振幅・周波数・減衰といった
// 見え方のチューニングはカメラ側(CameraShake)が一元的に持つ。発生源ごとに
// 振幅を持たせると、同じ画面の揺れなのに基準がバラバラになり、
// どれをどれだけ強くしたのか横並びで比べられなくなる
struct CameraShakeRequestEvent
{
    // 積む衝撃量(0..1)。1.0 でそのカメラの最大の揺れ
    //
    // 同一フレームに複数届いた場合、受け側は「最大値」だけを採用する
    // ドローン多数に同時命中すると要求も同数飛ぶため、足し合わせると
    // 1回の斬撃で最大の揺れに飽和してしまう
    float trauma;
};

// 指定エンティティを追従しているカメラの画角を瞬間的に変える要求（FOVパンチ）
// 設定後は CameraBodyTPSComponent::fovDamping により自動的に元へ戻る
struct CameraFovPunchRequestEvent
{
    CCL::ECS::EntityID targetID; // このエンティティを追従しているカメラが対象
    float              fov;      // 瞬間的に設定する画角（度）
};

// 導入カメラ演出（ボス登場のオービット等）の再生要求
// ゲーム開始時は playOnStart で自動再生されるため、これは
// 「形態移行でもう一度見せたい」といった任意タイミング用
struct CameraIntroPlayRequestEvent
{
    // 再生したいカメラのEntity。InvalidEntityID ならすべての導入カメラを再生する
    CCL::ECS::EntityID cameraID = CCL::ECS::InvalidEntityID;
};

// 導入カメラ演出が終わった通知（スキップによる終了も含む）
// ゲーム側はこれを受けて、戦闘開始・UI表示などを始める
// カメラ自身の優先度降格とプレイヤー操作の返却は演出システムが行うので、
// 受け手がそれらを気にする必要はない
struct CameraIntroFinishedEvent
{
    CCL::ECS::EntityID cameraID = CCL::ECS::InvalidEntityID;
    bool               skipped  = false; // プレイヤーがスキップしたか
};
