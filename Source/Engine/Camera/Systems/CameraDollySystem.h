#pragma once
// ===========================================================================
// 概要: スプライン軌道上のカメラ（台車）を等速で進め、VirtualCamera に位置と注視点を
//       書き込む System。
//
// 責務:
//   - 軌道 JSON の遅延読み込みと LUT の構築
//   - 距離(m)で進行度を進め、Catmull-Rom で位置を求める（弧長パラメータ化による等速移動）
//   - 先読み距離ぶん先の軌道上の点を注視する
//   - スプライン計算を static 関数としてエディタへ公開する
//
// 備考: 進行には引数の dt ではなく、同じ Entity の TimeStateComponent::localDt を使う。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Camera/Components/VirtualCameraComponents.h"
#include "Engine/Camera/Components/CameraDollyCartComponent.h"
#include "Engine/Time/TimeState.h"

#include <vector>

class CameraDollySystem : public CCL::ECS::IfSystem<
                              CameraDollySystem,
                              CCL::ECS::Write<VirtualCamera>,
                              CCL::ECS::Write<CameraDollyCartComponent>,
                              CCL::ECS::Read<TimeStateComponent>> // localDt を読む
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
    // --- スプライン評価 ---
    // 制御点を取り出す。loop なら端をまたいで巻き戻す（閉曲線になる）。
    static const DirectX::SimpleMath::Vector3 &
    GetPoint(const std::vector<DirectX::SimpleMath::Vector3> &pts, int index, bool loop);

    // パラメータ t（区間番号 + 小数）位置の座標を Catmull-Rom で求める。
    static DirectX::SimpleMath::Vector3
    EvaluateCatmullRom(const std::vector<DirectX::SimpleMath::Vector3> &pts, float t, bool loop);

    // t の最大値。開曲線なら points-1、閉曲線なら points（最後の点→始点の区間ぶん）。
    static float MaxParam(const CameraPathAsset &asset);

    // --- 弧長パラメータ化 ---
    // 「距離 → t」の対応表を作る。JSONロード直後に1回だけ実行する。
    static void BuildLookupTable(CameraPathAsset &asset);

    // 距離(m)からパラメータ t を逆算する（LUTの二分探索＋線形補間）。
    static float GetTFromDistance(const CameraPathAsset &asset, float targetDist);

    // ループ軌道用に距離を [0, totalLength) へ巻き戻す。
    static float WrapDistance(float dist, float totalLength);

private:
    /**
     * @brief pathAsset の JSON を読み込み、LUT まで構築する。
     * @param cart 読み込む台車。未読み込みで、まだ失敗していないときだけ試す。失敗時は hasLoadError を立てる。
     * @note 同期 I/O と JSON パースを Update 内で行うため、初回フレームでヒッチする。
     */
    void LoadPathIfNeeded(CameraDollyCartComponent &cart) const;
};
