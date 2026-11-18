#pragma once
// ===========================================================================
// 概要: プレイヤーを追う TPS カメラの姿勢と画角を毎フレーム決める System
//
// 責務:
//   - 視点の入力で向きを回し、ロックオン中は敵の方へ補正する
//   - 演出の上書き（framingOverrides）を合成して、普段の構図へ足す
//   - 地形を避けて、カメラを置く位置を決める
//   - マウスで視点を回す間は、カーソルを固定する
//
// 備考: CameraTargetBindSystem（R00b）が target を決めた後に走る
//       SetMouseLock をフレームに1回にするため、カメラは直列で回す
// ===========================================================================

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Camera/Components/VirtualCameraComponents.h"
#include "Game/Logics/Camera/PlayerCameraComponent.h"
#include "Game/Logics/Camera/PlayerCameraLockOnComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"

#include <memory>
#include <vector>

class IInputAPI;
class IPhysicsAPI;

class PlayerCameraSystem : public CCL::ECS::IfSystem<
    PlayerCameraSystem,
    CCL::ECS::Write<VirtualCamera>,
    CCL::ECS::Write<PlayerCameraComponent>, // 向きと補間中の状態を書く
    CCL::ECS::Write<TransformComponent>>    // カメラ自身の位置と向きを書く
{
public:
    PlayerCameraSystem() : IfSystem("PlayerCameraSystem") {}
    virtual ~PlayerCameraSystem() = default;

    void Update(float dt) override;

    // 揺れはこの System が決めた位置に足すので、揺れより前に走る
    std::vector<const char *> GetRunBefore() const override { return {"CameraShakeSystem"}; }

    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override
    {
        auto types = IfSystem::GetWriteTypes();
        types.push_back(CCL::ECS::TypeInfo<PlayerCameraLockOnComponent>::ID()); // 無いカメラもあるので ForEach に入れない
        return types;
    }

private:
    // =======================================================================
    // 作業データ
    // =======================================================================

    // 1フレーム分の視点の入力と、入力を止める条件
    // カメラの台数によらず、フレームの初めに1回だけ作る
    struct FrameInput
    {
        float lookX         = 0.0f;  // 右スティックの左右（-1〜1）
        float lookY         = 0.0f;  // 右スティックの上下（-1〜1）
        float mouseDX       = 0.0f;  // マウスの水平移動量（ピクセル）
        float mouseDY       = 0.0f;  // マウスの垂直移動量（ピクセル）
        bool  rmbHeld       = false; // 右クリックを押しているか
        bool  hasManualLook = false; // しきい値を超えて視点を動かしたか
        bool  cursorFree    = false; // カーソルを固定しないか
        bool  blockLook     = false; // 視点の入力を受けないか
    };

    // カメラ1台を更新する間だけ使う作業セット。参照を持つので保存しない
    // enemy があるときは、lockOn も必ずある
    struct CameraContext
    {
        VirtualCamera               &vcam;    // このカメラが出す結果
        PlayerCameraComponent       &tps;     // 調整値と状態
        PlayerCameraLockOnComponent *lockOn;  // ロックオンの設定。無ければ nullptr（所有しない）
        TransformComponent          &trans;   // カメラ自身の Transform
        const TransformComponent    &target;  // 追従する相手
        const TransformComponent    *enemy;   // ロックオンの敵。無ければ nullptr（所有しない）
        const FrameInput            &in;      // このフレームの視点の入力
        IPhysicsAPI                 *physics; // 地形の判定。無ければ nullptr（所有しない）
        float                        dt;      // 経過時間（秒）

        CameraFramingParams framing{};                    // 合成した上書きの足し込み量
        bool                framingMovesDistance = false; // 距離を動かす上書きがあるか
    };

    // =======================================================================
    // フレーム単位
    // =======================================================================

    // 視点の入力を読み、入力を止める条件を決める
    FrameInput ReadFrameInput(const std::shared_ptr<IInputAPI> &inputAPI) const;

    // カメラ1台の姿勢を決める。カーソルの固定を求めるなら true
    bool UpdateCamera(CameraContext &c) const;

    // 上書きのスロットを合成して、c.framing へ入れる
    // 注視点の寄せ先は、priority の大きい方を採る
    void ResolveFraming(CameraContext &c) const;

    // =======================================================================
    // どの向きから見るか
    // =======================================================================

    // ヨーとピッチ（currentYaw / currentPitch）を決める
    void UpdateOrbitAngles(CameraContext &c) const;

    // 手で動かした分を、ヨーとピッチへ足す
    void ApplyManualLook(CameraContext &c) const;

    // 敵が中央から外れた分だけ、ヨーを敵の方へ回す
    void ApplyLockOnYawAssist(CameraContext &c) const;

    // ピッチを、ロックオンの pitch へ寄せる
    void ApplyLockOnPitchAssist(CameraContext &c) const;

    // =======================================================================
    // どこを中心に、どこへ置くか
    // =======================================================================

    // 注視点と回り込みの中心を、1フレーム分追従させる
    // 戻り値は、地形を考える前の距離（m）
    float UpdateFraming(CameraContext &c) const;

    // 地形を避けたカメラの位置を返し、currentCamDistance を進める
    // viewDir はカメラが見ている向き
    DirectX::SimpleMath::Vector3 ResolveCameraPosition(
        CameraContext &c, const DirectX::SimpleMath::Vector3 &viewDir,
        float desiredDistance) const;

    // 頭の高さへ上げた位置が地形に入っていれば、押し戻した位置を返す
    DirectX::SimpleMath::Vector3 ResolveHeadLiftCollision(
        const CameraContext &c, const DirectX::SimpleMath::Vector3 &liftedPos) const;

    // =======================================================================
    // 出力
    // =======================================================================

    // 画角を進め、姿勢を Transform と VirtualCamera へ書き出す
    void WriteCameraResult(CameraContext &c, const DirectX::SimpleMath::Vector3 &finalPos,
                           const DirectX::SimpleMath::Vector3 &viewDir) const;
};
