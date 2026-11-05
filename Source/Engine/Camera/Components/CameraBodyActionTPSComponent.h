#pragma once
// アクション向け TPS カメラの調整値と実行時の状態。挙動は CameraActionTPSSystem が計算する
// ロックオンの状態（lockOnTarget / isLockOnActive）は、ゲーム側の PlayerLockOnSystem が毎フレーム書く

#include "Engine/Serialization/Core/EntityLink.h"
#include "Engine/ECS/Common/CCL_Types.h"
#include "Engine/Graphics/Components/PostProcess/PostEffectOverrides.h"
#include <SimpleMath.h>

// 演出がカメラへ出す構図の上書き。普段の構図への「足し込み量」で表す
//   値は足し込みなので、複数の演出が重なっても weight を掛けて足すだけで合成できる
//   カメラは要求の理由（ラッシュなのか必殺技なのか）を知らない
struct CameraFramingParams
{
    float distanceOffset = 0.0f; // 距離の足し込み（m）。負で寄る、正で引く
    float fovOffset      = 0.0f; // 画角の足し込み（度）
    float pitchOffset    = 0.0f; // ピッチの足し込み（度）。正でカメラが低くなり見上げる
    float yawOffset      = 0.0f; // ヨーの足し込み（度）。斜めから映す
    float roll           = 0.0f; // 画面の傾き（度）。視線の軸まわりに上方向を回す（ダッチアングル）。符号で傾く向きが変わる
    float followBoost    = 0.0f; // 追従の速さの足し込み（1/秒）。突進するキャラに遅れず付いていく

    float              focus       = 0.0f;                      // 注視点を focusTarget へ寄せる割合（0〜1）
    CCL::ECS::EntityID focusTarget = CCL::ECS::InvalidEntityID; // 注視点を寄せる先。Invalid なら寄せない
};

struct CameraBodyTPSComponent
{
    // --- ターゲット ---
    CCL::Serialization::EntityLink target;                            // 追従対象の Entity
    DirectX::SimpleMath::Vector3   targetOffset = {0.0f, 1.5f, 0.0f}; // 注視点のオフセット（m）。キャラクターの頭・肩の高さ

    // --- 距離と画角 ---
    float distance = 6.0f;  // ターゲットからの基本距離（m）
    float baseFov  = 45.0f; // 通常時の画角（度）
    float dashFov  = 70.0f; // wideFovRequest 中の画角（度）
    // 画角を dashFov 側へ広げてほしいという要求。ゲーム側（PlayerDashSystem 等）が毎フレーム設定する
    // カメラがゲーム固有の PlayerDashComponent を読みに行かずに済むようにするためのフィールド
    bool wideFovRequest = false;

    // --- 寄り（クローズアップ）要求：切り払い準備などで一時的にカメラを近づける ---
    // true の間だけ基準距離を closeUpDistance へ寄せる。ゲーム側が毎フレーム設定する
    bool  closeUpRequest  = false;
    float closeUpDistance = 3.5f; // 寄り要求中の基準距離（m）。distance より近くする

    // --- 構図の上書き（演出からの要求） ---
    // 演出側が自分の発生源キーでスロットを取り、毎フレーム値と weight を書く。終わったら Release
    // 1本の bool を共有すると、後から走る書き手が毎フレーム false を書いて他の演出を潰すため、
    // 発生源ごとに分けて CameraActionTPSSystem が1か所で合成する。保存しない
    PostEffectOverrides<CameraFramingParams, 4> framingOverrides;

    float framingFollowDamping = 30.0f; // 上書き中、距離を外へ広げる速さ（地形で寄った後の戻しより速くする）
    float framingMaxLookShift  = 4.0f;  // 上書きで注視点を寄せる量の上限（m）。プレイヤーを画面内に残す

    // --- オービット（回転） ---
    // pitch が正だとカメラは pivot より下に来て見上げ、負なら上に来て見下ろす
    // minPitch は見下ろし側の限界、maxPitch は見上げ側（地面へ潜る側）の限界
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
    float sensitivityScale        = 1.0f;  // 視点感度の倍率（マウス・スティック共通）。ゲームの設定画面が毎フレーム書く。保存しない
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
    float collisionOutDamping  = 6.0f;  // 障害物がなくなったとき外側へ戻す速さ（小さいほどゆっくり戻る）

    // カメラが寄ったとき、足元ではなく頭付近の高さへ持ち上げる強さ
    // 引き込み量に応じてカメラの Y を pivot（頭）の高さへ寄せる。0 = そのまま（足元に落ちる）、1 = 完全に頭の高さ
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
    // （lockOnManualOverrideTimer の間）を除いてロックオン中ずっと寄せ続ける
    //   符号は上の「ピッチの符号」と同じ。正にするほどカメラが低くなって見上げる
    //   ドローンが上空から来るゲームなので、既定は正（＝低く構えて空を入れる）
    float lockOnPitch = 8.0f;
    // 上の寄せの追従速度（CCL::Math::Damp の speed）。8 なら約 0.4 秒で 95% 寄る
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
