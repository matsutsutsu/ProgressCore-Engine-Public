/// @file  CameraDollySystem.h
/// @brief スプライン軌道上のカメラ（台車）を移動させ、VirtualCamera に座標を書き込むシステム。

#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Camera/Components/VirtualCameraComponents.h"
#include "Engine/Camera/Components/CameraDollyCartComponent.h"
// ※TimeStateComponentの想定パス
#include "Engine/Time/TimeState.h"

#include <vector>

// ECS指示書に基づき、名前空間でクラスを囲まない
class CameraDollySystem : public CCL::ECS::IfSystem<
                              CameraDollySystem,
                              CCL::ECS::Write<VirtualCamera>,
                              CCL::ECS::Write<CameraDollyCartComponent>,
                              CCL::ECS::Read<TimeStateComponent>> // 時間管理厳守: TimeScaleを読む
{
  public:
    CameraDollySystem()
        : IfSystem("CameraDollySystem")
    {
    }
    virtual ~CameraDollySystem() = default;

    void Update(float dt) override;

    // =====================================================================
    // パスデータに対する純粋な計算。CameraPathEditorWindow からも呼ぶため public。
    // （エディタで制御点を編集したら LUT を作り直さないと、totalLength が古いまま
    //   台車の速度と位置がずれる）
    // =====================================================================
    // --- スプライン評価 ---------------------------------------------------
    /// @brief 制御点を取り出す。isLoop なら端をまたいで巻き戻す（＝閉曲線になる）。
    static const DirectX::SimpleMath::Vector3 &
    GetPoint(const std::vector<DirectX::SimpleMath::Vector3> &pts, int index, bool loop);

    /// @brief パラメータ t（区間番号 + 小数）位置の座標を Catmull-Rom で求める。
    static DirectX::SimpleMath::Vector3
    EvaluateCatmullRom(const std::vector<DirectX::SimpleMath::Vector3> &pts, float t, bool loop);

    /// @brief t の最大値。開曲線なら points-1、閉曲線なら points（最後の点→始点の区間ぶん）。
    static float MaxParam(const CameraPathAsset &asset);

    // --- 弧長パラメータ化 -------------------------------------------------
    /// @brief 「距離 → t」の対応表を作る。JSONロード直後に1回だけ実行する。
    static void BuildLookupTable(CameraPathAsset &asset);

    /// @brief 距離(m)からパラメータ t を逆算する（LUTの二分探索＋線形補間）。
    static float GetTFromDistance(const CameraPathAsset &asset, float targetDist);

    /// @brief ループ軌道用に距離を [0, totalLength) へ巻き戻す。
    static float WrapDistance(float dist, float totalLength);

  private:
    // --- リソース ---------------------------------------------------------
    /// @brief pathAsset の JSON を読み込み、LUT まで構築する。失敗時は hasLoadError を立てる。
    /// @note  同期I/Oなので初回フレームでヒッチする。将来はアセットマネージャへ移すこと。
    void LoadPathIfNeeded(CameraDollyCartComponent &cart) const;
};
