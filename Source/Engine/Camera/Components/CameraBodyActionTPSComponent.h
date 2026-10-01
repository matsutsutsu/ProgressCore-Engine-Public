#pragma once
// ===========================================================================
// 概要: アクション向け TPS カメラの調整パラメータと実行時状態。
//
// 責務:
//   - 追従対象・距離・画角・回転制限・操作感度の調整値を持つ
//   - カメラ衝突（地形へのめり込み回避）とルックアヘッドの調整値を持つ
//   - ロックオン中のフレーミングと自動補正の調整値を持つ
//   - 補間用の実行時状態を持つ（シリアライズしない）
//
// 備考: 挙動は CameraActionTPSSystem が計算する。ロックオン状態
//       （lockOnTarget / isLockOnActive）はゲーム側の PlayerLockOnSystem が毎フレーム書く。
// ===========================================================================

#include "Engine/Serialization/Core/EntityLink.h"
#include <SimpleMath.h>

struct CameraBodyTPSComponent
{
    // --- ターゲット ---
    CCL::Serialization::EntityLink target;                            // 追従対象の Entity
    DirectX::SimpleMath::Vector3   targetOffset = {0.0f, 1.5f, 0.0f}; // 注視点のオフセット（m）。キャラクターの頭・肩の高さ

    // --- 距離と画角 ---
    float distance = 6.0f;  // ターゲットからの基本距離（m）
    float baseFov  = 45.0f; // 通常時の画角（度）
    float dashFov  = 70.0f; // wideFovRequest 中の画角（度）
    // 画角を dashFov 側へ広げてほしいという要求。ゲーム側（PlayerDashSystem 等）が毎フレーム設定する。
    // カメラがゲーム固有の DashComponent を読みに行かずに済むようにするためのフィールド。
    bool wideFovRequest = false;

    // --- 寄り（クローズアップ）要求：切り払い準備などで一時的にカメラを近づける ---
    // true の間だけ基準距離を closeUpDistance へ寄せる。ゲーム側が毎フレーム設定する。
    bool  closeUpRequest  = false;
    float closeUpDistance = 3.5f; // 寄り要求中の基準距離（m）。distance より近くする

    // --- オービット（回転） ---
    // ピッチの符号: CameraActionTPSSystem は
    //     backwardDir.y = sin(pitch) / finalPos = pivot - backwardDir * distance
    //   で配置するため、pitch が正のときカメラは pivot より「下」に来る。
    //     ・pitch が負 → カメラが上 → 見下ろし
    //     ・pitch が正 → カメラが下 → 見上げ
    //   よって minPitch は見下ろし側の限界、maxPitch は見上げ側（地面へ潜る側）の限界。
    float currentYaw   = 0.0f;   // 現在の水平角（度）。プレハブの値は初期ヨー角として使われる
    float currentPitch = 15.0f;  // 現在の垂直角（度）。プレハブの値は初期ピッチ角として使われる
    float minPitch     = -70.0f; // 見下ろし側の限界（度）。負に振るほど真上から覗き込む
    float maxPitch     = 80.0f;  // 見上げ側の限界（度）。正に振るほどカメラが低くなり地面に潜る

    float lookSpeedX = 120.0f; // ゲームパッド右スティック X 軸の回転速度（度/秒）。dt で積分する
    float lookSpeedY = 120.0f; // ゲームパッド右スティック Y 軸の回転速度（度/秒）。dt で積分する

    // --- マウス視点操作（移動量入力なので dt を掛けない） ---
    float mouseSensitivityX       = 0.15f; // マウス水平感度（度/ピクセル）
    float mouseSensitivityY       = 0.15f; // マウス垂直感度（度/ピクセル）
    bool  mouseInvertY            = false; // 上下反転（true = マウス下でカメラ上向き）
    bool  mouseLookRequiresButton = false; // true = 右クリック押下中のみ視点回転。false = 常時マウス視点
    bool  enableCursorLock        = true;  // マウスルック中、カーソルを画面中央へ固定して非表示にする（画面端で止まるのを防ぐ）

    // --- 追従の指数減衰（CCL::Math::Damp の speed。大きいほど速く目標へ着く） ---
    float positionDamping = 12.0f; // 注視点・回り込み中心の追従速度。低いほど遅れて付いてくる
    float fovDamping      = 8.0f;  // 画角の追従速度

    // --- カメラ衝突（地形・建物へのめり込み回避） ---
    bool  collisionEnabled     = true;  // 地形との衝突回避を行うか
    float collisionRadius      = 0.3f;  // 衝突面から離す余白（m）。ニアクリップでのめり込みを防ぐ
    float collisionMinDistance = 0.8f;  // 衝突で寄っても、これ以上プレイヤーへ近づかない下限距離（m）
    float collisionInDamping   = 30.0f; // 障害物に当たったとき内側へ寄せる速さ（大きいほど素早い）
    float collisionOutDamping  = 6.0f;  // 障害物が無くなったとき外側へ戻す速さ（小さいほどゆっくり戻る）

    // カメラが寄ったとき、足元ではなく頭付近の高さへ持ち上げる強さ。
    // 引き込み量に応じてカメラの Y を pivot（頭）の高さへ寄せる。0 = そのまま（足元に落ちる）、1 = 完全に頭の高さ。
    float collisionHeadBias = 1.0f;

    float currentCamDistance = 0.0f; // 衝突を反映した現在のカメラ距離（m、実行時状態）

    // --- ルックアヘッド（進行方向への注視） ---
    float lookaheadMultiplier = 0.3f; // 速度に掛けて注視点を前方へずらす係数（秒）
    float lookaheadDamping    = 3.0f; // ルックアヘッドの追従速度。低いほど遅れて付いてくる

    DirectX::SimpleMath::Vector3 currentLookahead = {0.0f, 0.0f, 0.0f}; // 現在のルックアヘッド量（m、実行時状態）

    // =======================================================================
    // ロックオン（PlayerLockOnSystem が毎フレーム書く）
    // =======================================================================

    CCL::Serialization::EntityLink lockOnTarget;           // ロックオン中の敵
    bool                           isLockOnActive = false; // ロックオン中か

    // ロックオン中に保つ見上げ角（度）。ヨーの自動補正と同じく、手動操作の直後
    // （lockOnManualOverrideTimer の間）を除いてロックオン中ずっと寄せ続ける。
    //   符号は上の「ピッチの符号」と同じ。正にするほどカメラが低くなって見上げる。
    //   ドローンが上空から来るゲームなので、既定は正（＝低く構えて空を入れる）。
    float lockOnPitch = 8.0f;
    // 上の寄せの追従速度（CCL::Math::Damp の speed）。8 なら約 0.4 秒で 95% 寄る。
    float lockOnPitchDamping = 8.0f;

    // --- フレーミング ---
    float lockOnSubjugationRatio = 0.35f; // 注視点をプレイヤーから敵側へ寄せる割合（0 = プレイヤー中心）
    float lockOnMaxLookShift     = 3.0f;  // 注視点を敵側へずらす量の上限（m）。プレイヤーを画面内に残すため
    float lockOnDistanceGain     = 0.35f; // プレイヤーと敵の距離に掛けて、カメラを追加で引く係数
    float lockOnMaxExtraDistance = 8.0f;  // 追加で引く距離の上限（m）

    // --- カメラアシスト（ヨーの自動補正） ---
    bool  lockOnAllowManualCamera  = true;   // ロックオン中も手動のカメラ操作を受け付けるか
    float lockOnYawDeadZone        = 20.0f;  // 敵がこの角度以内なら自動補正しない（度）
    float lockOnAssistStrength     = 3.0f;   // デッドゾーンを超えた角度に掛けて補正速度にする係数（/秒）
    float lockOnMaxCorrectionSpeed = 180.0f; // 自動補正の最大速度（度/秒）
    float lockOnManualOverrideTime = 0.25f;  // プレイヤーがカメラを操作した後、自動補正を止める時間（秒）

    // =======================================================================
    // 実行時状態（シリアライズしない）
    // =======================================================================

    float lockOnManualOverrideTimer = 0.0f; // 自動補正を止めている残り時間（秒）

    DirectX::SimpleMath::Vector3 currentLookAt = {0.0f, 0.0f, 0.0f}; // 補間中の注視点
    DirectX::SimpleMath::Vector3 currentPivot  = {0.0f, 0.0f, 0.0f}; // 補間中の回り込み中心（ロックオン時はプレイヤー基準）
    bool                         isFirstFrame  = true;               // true なら次のフレームで補間せず目標位置へ置く
};
