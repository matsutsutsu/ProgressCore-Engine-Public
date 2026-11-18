#pragma once
// ===========================================================================
// 概要: PlayerCameraComponent を持つカメラの位置・向き・画角を毎フレーム計算する System
//
// 責務:
//   - スティックとマウスの入力でオービット角を更新し、ロックオン中はヨーとピッチを自動で補正する
//   - 地形へのめり込みを避け（レイキャスト）、進む向きを先読みする
//   - 演出からの構図の上書き（framingOverrides）を合成して、距離・画角・角度・注視点へ足す
//   - 画角パンチの要求を受け、マウスルック中はカーソルを固定する
//
// 備考: R00b で CameraTargetBindSystem が target を決めておくこと。未解決ならそのカメラは黙って飛ばす
//       ここ（R01_Camera）で姿勢を確定し、R01b の CameraBrainSystem が採用・ブレンドする。揺れは後段の CameraShakeSystem
//       カーソルの固定は全カメラを見てから1つに決める（SetMouseLock はフレームに1回）ので、直列の ForEach で回す
// ===========================================================================

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Camera/Components/VirtualCameraComponents.h"
#include "Game/Logics/Camera/PlayerCameraComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Core/GameEvents.h"

#include <memory>
#include <mutex>
#include <vector>

class IInputAPI;
class IPhysicsAPI;

class PlayerCameraSystem : public CCL::ECS::IfSystem<
    PlayerCameraSystem,
    CCL::ECS::Write<VirtualCamera>,
    CCL::ECS::Write<PlayerCameraComponent>, // 内部状態(Pitch/Yaw等)を更新するためWrite
    CCL::ECS::Write<TransformComponent>>    // カメラ自身のTransformを確定させるためWrite
{
public:
    PlayerCameraSystem() : IfSystem("PlayerCameraSystem") {}
    virtual ~PlayerCameraSystem() = default;

    // FOV パンチ要求（CameraFovPunchRequestEvent）の購読を始める
    // 購読では要求をためるだけにして、画角を書き換えるのは Update にする（揺れの CameraShakeSystem と同じ形）
    void Initialize() override;

    // 全 TPS カメラの位置・向き・画角を、このフレームの分だけ確定させる
    void Update(float dt) override;

    // 揺れはこの System が書いた結果に足すので、揺れより前に走る
    // 揺れの側（Engine）が Game の System 名を知らずに済むよう、こちらで宣言する
    std::vector<const char *> GetRunBefore() const override { return {"CameraShakeSystem"}; }

private:
    // =======================================================================
    // 作業データ
    // =======================================================================

    // 1フレーム分の視点入力と、入力を無効にする条件
    // カメラ台数に関係なくフレーム先頭で1度だけ作り、各カメラへ読み取り専用で渡す
    struct FrameInput
    {
        float lookX         = 0.0f;  // 右スティックX の倒し量（-1..1）。速度入力なので dt を掛ける
        float lookY         = 0.0f;  // 右スティックY の倒し量（-1..1）
        float mouseDX       = 0.0f;  // マウス水平移動量（ピクセル/フレーム）。移動量なので dt を掛けない
        float mouseDY       = 0.0f;  // マウス垂直移動量（ピクセル/フレーム）
        bool  rmbHeld       = false; // 右クリック押下中か
        bool  hasManualLook = false; // しきい値を超える視点操作があったか（ロックオン補正の停止判定用）
        bool  cursorFree    = false; // カーソルを固定しない（ポーズ中・UI操作中・決着の画面・ウィンドウ非アクティブ）
        bool  blockLook     = false; // カットシーン中。視点入力を一切受け付けない
    };

    // カメラ1台を更新する間だけ生きる作業セット
    //   各段階が必要とするもの（対象・入力・dt）はほぼ共通なので1つに束ねる
    //   束ねないとすべての宣言が引数の列で埋まり、何をする関数なのかが読めなくなる
    //   参照を持つので Update のループ内だけで使い、保存しない
    struct CameraContext
    {
        VirtualCamera            &vcam;    // このカメラが出す答え（Brain が読む）
        PlayerCameraComponent    &tps;     // 調整値と実行時状態
        TransformComponent       &trans;   // カメラ自身の Transform
        const TransformComponent &target;  // 追従対象。必ず存在する
        const TransformComponent *enemy;   // ロックオン対象。していなければ nullptr（所有しない）
        const FrameInput         &in;      // このフレームの視点入力
        IPhysicsAPI              *physics; // 地形判定用。nullptr なら衝突回避をしない（所有しない）
        float                     dt;      // このフレームの経過時間（秒）

        CameraFramingParams framing{};                    // 演出の上書きを合成した足し込み量（UpdateCamera の先頭で決める）
        bool                framingMovesDistance = false; // 距離を動かしている上書きが1つでもあるか（距離を外へ戻す速さの切り替えに使う）
    };

    // =======================================================================
    // フレーム単位
    // =======================================================================

    // ためた FOV パンチ要求を取り出して空にする
    std::vector<CameraFovPunchRequestEvent> TakeFovPunches();

    // 視点入力を読み、入力を無効にする条件を確定する
    FrameInput ReadFrameInput(const std::shared_ptr<IInputAPI> &inputAPI) const;

    // カメラ1台分の姿勢を確定させる。このカメラがカーソルの固定を求めるなら true
    bool UpdateCamera(CameraContext &c) const;

    // 演出の上書きスロットを priority の小さい順に合成し、c.framing / c.framingMovesDistance へ入れる
    // 足し込み量は weight を掛けて足す。注視点の寄せ先は、後に重なる方（priority の大きい方）を採る
    void ResolveFraming(CameraContext &c) const;

    // =======================================================================
    // どの向きから見るか（オービット角）
    // =======================================================================

    // オービット角（currentYaw / currentPitch）を確定させる
    void UpdateOrbitAngles(CameraContext &c) const;

    // 手動の視点入力をオービット角へ積む
    void ApplyManualLook(CameraContext &c) const;

    // 敵が画面中央から外れた分だけ、ヨー（左右）を自動で補正する
    void ApplyLockOnYawAssist(CameraContext &c) const;

    // ピッチ（上下）を lockOnPitch へ寄せ、ロックオン中の見上げ角を保つ
    void ApplyLockOnPitchAssist(CameraContext &c) const;

    // =======================================================================
    // どこを中心に、どこへ置くか
    // =======================================================================

    // 注視点と回り込みの中心を決め、1フレーム分追従する（結果は currentLookAt / currentPivot）
    // 戻り値は、衝突を考える前の基準距離（m）。実際の距離は ResolveCameraPosition が決める
    float UpdateFraming(CameraContext &c) const;

    // pivot から見てカメラが立つ方向（正規化済み）を、角度（度）から求める。pitch が正ならカメラは pivot より下
    static DirectX::SimpleMath::Vector3 ComputeBackwardDir(float yawDeg, float pitchDeg);

    // 地形を避けたカメラ位置を求め、currentCamDistance を更新する
    DirectX::SimpleMath::Vector3 ResolveCameraPosition(
        CameraContext &c, const DirectX::SimpleMath::Vector3 &backwardDir,
        float desiredDistance) const;

    // 頭の高さへ持ち上げた位置が地形に入っていれば、押し戻した位置を返す
    DirectX::SimpleMath::Vector3 ResolveHeadLiftCollision(
        const CameraContext &c, const DirectX::SimpleMath::Vector3 &liftedPos) const;

    // =======================================================================
    // 出力
    // =======================================================================

    // 画角を進め、確定した姿勢を Transform と VirtualCamera へ書き出す
    void WriteCameraResult(CameraContext &c, const DirectX::SimpleMath::Vector3 &finalPos,
                           const DirectX::SimpleMath::Vector3 &backwardDir) const;

    // =======================================================================
    // 届いた要求
    // =======================================================================

    // 購読は発行した System のタスクで走るので、Update と同時に触られうる。mutex で守る
    std::mutex                              m_punchMutex;
    std::vector<CameraFovPunchRequestEvent> m_pendingPunches; // まだ反映していない FOV パンチ要求
};
