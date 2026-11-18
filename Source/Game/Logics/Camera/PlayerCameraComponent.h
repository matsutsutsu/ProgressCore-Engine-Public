#pragma once
#include "Engine/Serialization/Core/EntityLink.h"
#include "Engine/ECS/Common/CCL_Types.h"
#include "Engine/Graphics/Components/PostProcess/PostEffectOverrides.h"
#include <SimpleMath.h>
#include <cmath>

// 演出がカメラへ出す構図の上書き。普段の構図への足し込み量で表す
// 足し込みなので、演出が重なっても weight を掛けて足せば合成できる
struct CameraFramingParams
{
    float distanceOffset = 0.0f; // 距離の足し込み（m）。負で寄る
    float fovOffset      = 0.0f; // 画角の足し込み（度）
    float pitchOffset    = 0.0f; // ピッチの足し込み（度）。正で見上げる
    float yawOffset      = 0.0f; // ヨーの足し込み（度）
    float roll           = 0.0f; // 画面の傾き（度）
    float followBoost    = 0.0f; // 追従の速さの足し込み（1/秒）

    float              focus       = 0.0f;                      // 注視点を寄せる割合（0〜1）
    CCL::ECS::EntityID focusTarget = CCL::ECS::InvalidEntityID; // 注視点を寄せる先。無効なら寄せない
};

// プレイヤーを追う TPS カメラの調整値と状態
// ロックオン中の動きは PlayerCameraLockOnComponent が持つ
struct PlayerCameraComponent
{
    // --- ターゲット ---
    CCL::Serialization::EntityLink target;                            // 追従する Entity
    DirectX::SimpleMath::Vector3   targetOffset = {0.0f, 1.5f, 0.0f}; // 注視点のオフセット（m）

    // --- 距離と画角 ---
    float distance = 6.0f;  // 普段の距離（m）
    float baseFov  = 45.0f; // 普段の画角（度）

    // --- 構図の上書き ---
    // 演出は自分の発生源キーでスロットを取り、毎フレーム値と weight を書く
    // 終わったら Release する。保存しない
    PostEffectOverrides<CameraFramingParams, 4> framingOverrides;

    float framingFollowDamping = 30.0f; // 上書きで引くときの速さ
    float framingMaxLookShift  = 4.0f;  // 上書きで注視点をずらす上限（m）

    // 演出が書き、次の更新で一度だけ画角へ足して 0 に戻す
    // 足した後は fovDamping で普段の画角へ戻る。保存しない
    float pendingFovPunch = 0.0f; // 画角へ足す量（度）

    // 演出が書き、次の更新で視点の入力を捨てて false に戻す。書き続けている間だけ効く。保存しない
    // 作った構図を、プレイヤーが視点を回して崩さないようにする
    bool blockLookRequest = false; // 手で視点を回させないか

    // --- オービット ---
    // ピッチが正だと、カメラは pivot より下にいて見上げる
    float currentYaw   = 0.0f;   // 今のヨー（度）。プレハブの値は初期値
    float currentPitch = 15.0f;  // 今のピッチ（度）。プレハブの値は初期値
    float minPitch     = -70.0f; // 見下ろす側の限界（度）
    float maxPitch     = 80.0f;  // 見上げる側の限界（度）

    float lookSpeedX = 120.0f; // 右スティック左右の回転速度（度/秒）
    float lookSpeedY = 120.0f; // 右スティック上下の回転速度（度/秒）

    // --- マウス視点 ---
    float mouseSensitivityX       = 0.15f; // 水平の感度（度/ピクセル）
    float mouseSensitivityY       = 0.15f; // 垂直の感度（度/ピクセル）
    bool  mouseInvertY            = false; // 上下を反転するか
    float sensitivityScale        = 1.0f;  // 感度の倍率。設定画面が書く。保存しない
    bool  mouseLookRequiresButton = false; // 右クリック中だけ視点を回すか
    bool  enableCursorLock        = true;  // マウスルック中にカーソルを固定するか

    // --- 追従の速さ（CCL::Math::Damp の speed） ---
    float positionDamping = 12.0f; // 注視点と回り込み中心の追従
    float fovDamping      = 8.0f;  // 画角の追従

    // --- 地形との衝突 ---
    bool  collisionEnabled     = true;  // 地形を避けるか
    float collisionRadius      = 0.3f;  // 当たった面から離す余白（m）
    float collisionMinDistance = 0.8f;  // 寄っても保つ最小の距離（m）
    float collisionInDamping   = 30.0f; // 寄せる速さ
    float collisionOutDamping  = 6.0f;  // 戻す速さ

    float collisionHeadBias = 1.0f; // 寄ったとき頭の高さへ上げる強さ（0〜1）

    float currentCamDistance = 0.0f; // 衝突を反映した今の距離（m）

    // --- ルックアヘッド ---
    float lookaheadMultiplier = 0.3f; // 速度に掛けて注視点を前へずらす係数（秒）
    float lookaheadDamping    = 3.0f; // ずらす量の追従の速さ

    DirectX::SimpleMath::Vector3 currentLookahead = {0.0f, 0.0f, 0.0f}; // 今ずらしている量（m）

    // --- 実行時の状態（保存しない） ---
    DirectX::SimpleMath::Vector3 currentLookAt = {0.0f, 0.0f, 0.0f}; // 補間中の注視点
    DirectX::SimpleMath::Vector3 currentPivot  = {0.0f, 0.0f, 0.0f}; // 補間中の回り込み中心
    bool                         isFirstFrame  = true;               // 次のフレームは補間せずに置くか
};



// --- カメラの向きの決まり ---
// ヨー 0 で +X を見て、+Z の側へ回るほど増える（度）
// カメラは pivot から見て、見ている向きの逆側に立つ

// ヨーとピッチ（度）から、カメラが見ている向きを返す
// 長さは 1。ピッチが正なら見上げる向きになる
inline DirectX::SimpleMath::Vector3 CameraViewDir(float yawDeg, float pitchDeg)
{
    const float yawRad   = DirectX::XMConvertToRadians(yawDeg);
    const float pitchRad = DirectX::XMConvertToRadians(pitchDeg);

    DirectX::SimpleMath::Vector3 viewDir;
    viewDir.x = cosf(yawRad) * cosf(pitchRad);
    viewDir.y = sinf(pitchRad);
    viewDir.z = sinf(yawRad) * cosf(pitchRad);
    viewDir.Normalize();
    return viewDir;
}

// 水平の向きから、その向きを見るカメラのヨー（度）を返す
inline float CameraYawFromViewDir(const DirectX::SimpleMath::Vector3 &viewDir)
{
    return DirectX::XMConvertToDegrees(atan2f(viewDir.z, viewDir.x));
}
