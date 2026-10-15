#pragma once
// ===========================================================================
// 概要: CameraBodyTPSComponent を持つカメラの位置・向き・画角を毎フレーム計算する System。
//
// 責務:
//   - 右スティック／マウス入力でオービット角を更新する
//   - ロックオン中のフレーミングとヨーの自動補正
//   - 地形へのめり込み回避（レイキャスト）とルックアヘッド
//   - 演出からの構図の上書き（framingOverrides）を合成して、距離・画角・角度・注視点へ足す
//   - FOV パンチ要求（CameraFovPunchRequestEvent）の受信と、マウスルック中のカーソルロック
//
// 呼び出し順序（守らないと壊れる）:
//   - R00b で CameraTargetBindSystem が target を解決済みであること。本システムは
//     target.id を信じて引くだけで、未解決なら黙ってそのカメラを飛ばす。
//   - 本システムは R01_Camera で姿勢を確定させ、R01b の CameraBrainSystem が
//     VirtualCamera を読んで採用・ブレンドする。
//
// 備考:
//   - 画面揺れはここでは扱わない（CameraShakeSystem が後段で加算する）。
//   - 走査は直列の ForEach に限る。カーソルロックは全カメラを見た後に
//     1つの結論へ集約する（SetMouseLock はフレームに1回）ため、並列化できない。
// ===========================================================================

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Camera/Components/VirtualCameraComponents.h"
#include "Engine/Camera/Components/CameraBodyActionTPSComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"

#include <memory>

class IInputAPI;
class IPhysicsAPI;

class CameraActionTPSSystem : public CCL::ECS::IfSystem<
                                  CameraActionTPSSystem,
                                  CCL::ECS::Write<VirtualCamera>,
                                  CCL::ECS::Write<CameraBodyTPSComponent>, // 内部状態(Pitch/Yaw等)を更新するためWrite
                                  CCL::ECS::Write<TransformComponent>>     // カメラ自身のTransformを確定させるためWrite
{
public:
    CameraActionTPSSystem() : IfSystem("CameraActionTPSSystem") {}
    virtual ~CameraActionTPSSystem() = default;

    // FOV パンチ要求（CameraFovPunchRequestEvent）の購読を登録する
    void Initialize() override;

    // 全 TPS カメラの位置・向き・画角を、このフレームの分だけ確定させる
    void Update(float dt) override;

private:
    // =======================================================================
    // 作業データ
    // =======================================================================

    // 1フレーム分の視点入力と、入力を無効にする条件。
    // カメラ台数に関係なくフレーム先頭で1度だけ作り、各カメラへ読み取り専用で渡す。
    struct FrameInput
    {
        float lookX         = 0.0f;  // 右スティックX の倒し量（-1..1）。速度入力なので dt を掛ける
        float lookY         = 0.0f;  // 右スティックY の倒し量（-1..1）
        float mouseDX       = 0.0f;  // マウス水平移動量（ピクセル/フレーム）。移動量なので dt を掛けない
        float mouseDY       = 0.0f;  // マウス垂直移動量（ピクセル/フレーム）
        bool  rmbHeld       = false; // 右クリック押下中か
        bool  hasManualLook = false; // しきい値を超える視点操作があったか（ロックオン補正の停止判定用）
        bool  cursorFree    = false; // カーソルを固定しない（ポーズ中・UI操作中・ウィンドウ非アクティブ）
        bool  blockLook     = false; // カットシーン中。視点入力を一切受け付けない
    };

    // カメラ1台を更新する間だけ生きる作業セット。
    //   各段階が必要とするもの（対象・入力・dt）はほぼ共通なので1つに束ねる。
    //   束ねないと全ての宣言が引数の列で埋まり、何をする関数なのかが読めなくなる。
    //   参照を持つので Update のループ内だけで使い、保存しない。
    struct CameraContext
    {
        VirtualCamera            &vcam;    // このカメラが出す答え（Brain が読む）
        CameraBodyTPSComponent   &tps;     // 調整値と実行時状態
        TransformComponent       &trans;   // カメラ自身の Transform
        const TransformComponent &target;  // 追従対象。必ず存在する
        const TransformComponent *enemy;   // ロックオン対象。していなければ nullptr（所有しない）
        const FrameInput         &in;      // このフレームの視点入力
        IPhysicsAPI              *physics; // 地形判定用。nullptr なら衝突回避をしない（所有しない）
        float                     dt;      // このフレームの経過時間（秒）

        CameraFramingParams framing{};             // 演出の上書きを合成した足し込み量（UpdateCamera の先頭で決める）
        bool                framingActive = false; // 効いている上書きが1つでもあるか
    };

    // =======================================================================
    // フレーム単位
    // =======================================================================

    /**
     * @brief 視点入力を読み、入力を無効にする条件を確定する。
     * @param inputAPI 入力を読む API。
     * @return このフレームの視点入力。
     */
    FrameInput ReadFrameInput(const std::shared_ptr<IInputAPI> &inputAPI) const;

    /**
     * @brief カメラ1台分の姿勢を確定させる。
     * @param c このカメラの1フレーム分の作業データ（調整値・状態・追従対象）。
     * @return このカメラがカーソル固定を要求するなら true。
     */
    bool UpdateCamera(CameraContext &c) const;

    /**
     * @brief 演出の上書きスロットを priority の小さい順に合成し、c.framing / c.framingActive へ入れる。
     *
     * 足し込み量は weight を掛けて足す。注視点の寄せ先は、後に重なる（priority の大きい）方を採る。
     *
     * @param c このカメラの1フレーム分の作業データ（調整値・状態・追従対象）。
     */
    void ResolveFraming(CameraContext &c) const;

    // =======================================================================
    // どの向きから見るか（オービット角）
    // =======================================================================

    /**
     * @brief オービット角（currentYaw / currentPitch）を確定させる。
     * @param c このカメラの1フレーム分の作業データ（調整値・状態・追従対象）。
     */
    void UpdateOrbitAngles(CameraContext &c) const;

    /**
     * @brief 手動の視点入力をオービット角へ積む。
     * @param c このカメラの1フレーム分の作業データ（調整値・状態・追従対象）。
     */
    void ApplyManualLook(CameraContext &c) const;

    /**
     * @brief 敵が画面中央から外れた分だけヨー（左右）を自動補正する。
     * @param c このカメラの1フレーム分の作業データ（調整値・状態・追従対象）。
     */
    void ApplyLockOnYawAssist(CameraContext &c) const;

    /**
     * @brief ピッチ（上下）を lockOnPitch へ寄せ、ロックオン中の見上げ角を保つ。
     * @param c このカメラの1フレーム分の作業データ（調整値・状態・追従対象）。
     */
    void ApplyLockOnPitchAssist(CameraContext &c) const;

    // =======================================================================
    // どこを中心に、どこへ置くか
    // =======================================================================

    /**
     * @brief 注視点と回り込み中心を決め、そこへ1フレーム分追従する（結果は currentLookAt / currentPivot に入る）。
     * @param c このカメラの1フレーム分の作業データ（調整値・状態・追従対象）。
     * @return 衝突を考える前の基準距離（m）。実際の距離は ResolveCameraPosition が決める。
     */
    float UpdateFraming(CameraContext &c) const;

    /**
     * @brief 球面座標から、pivot から見てカメラが立つ方向を求める。
     * @param yawDeg   ヨー角（度）。
     * @param pitchDeg ピッチ角（度）。正のときカメラは pivot より下に来る（見上げる）。
     * @return pivot からカメラへの方向（正規化済み）。
     */
    static DirectX::SimpleMath::Vector3 ComputeBackwardDir(float yawDeg, float pitchDeg);

    /**
     * @brief 地形を避けたカメラ位置を求める。currentCamDistance を更新する。
     * @param c このカメラの1フレーム分の作業データ（調整値・状態・追従対象）。
     * @param backwardDir     pivot からカメラへの方向（ComputeBackwardDir の結果）。
     * @param desiredDistance 衝突を考える前の基準距離（m）。
     * @return カメラ位置（ワールド）。
     */
    DirectX::SimpleMath::Vector3 ResolveCameraPosition(
        CameraContext &c, const DirectX::SimpleMath::Vector3 &backwardDir,
        float desiredDistance) const;

    /**
     * @brief 頭の高さへ持ち上げた後の位置が地形へ入っていないか確認し、入っていれば押し戻す。
     * @param c このカメラの1フレーム分の作業データ（調整値・状態・追従対象）。
     * @param liftedPos 持ち上げた後のカメラ位置（ワールド）。
     * @return 地形に入っていなければ liftedPos、入っていれば押し戻した位置。
     */
    DirectX::SimpleMath::Vector3 ResolveHeadLiftCollision(
        const CameraContext &c, const DirectX::SimpleMath::Vector3 &liftedPos) const;

    // =======================================================================
    // 出力
    // =======================================================================

    /**
     * @brief 画角を進め、確定した姿勢を Transform と VirtualCamera へ書き出す。
     * @param c このカメラの1フレーム分の作業データ（調整値・状態・追従対象）。
     * @param finalPos    確定したカメラ位置（ワールド）。
     * @param backwardDir pivot からカメラへの方向。
     */
    void WriteCameraResult(CameraContext &c, const DirectX::SimpleMath::Vector3 &finalPos,
                           const DirectX::SimpleMath::Vector3 &backwardDir) const;
};
