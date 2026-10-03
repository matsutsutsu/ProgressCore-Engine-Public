#pragma once
// ===========================================================================
// 概要: タイトル画面のドローンの群れ。ドローンを自分で生成し、ボス像の頭上に
//       ゲーム本編と同じ円陣（HighOrbit）で並べる。
//
// 責務:
//   - ボス像（TitleBossStatue）を探し、その頭上にドローンを生成して持ち場に置く
//   - 回り続ける円陣の持ち場と向きを毎フレーム書く
//
// 備考: 円陣の半径・高さ・回転の速さは Formations_Basic.h の HighRing をそのまま使う。
//       タイトル用に値を写すと、本編を調整したときに片方だけ古くなり、タイトルと本編で輪が変わる。
//       陣形（IFormation）は通さない。陣形は DroneFormationSystem がボスの命令から計算するが、
//       タイトルにはボスの BT も命令も無い。そこで currentFormation = None にして陣形の計算を
//       飛ばし、HighRing の式で targetPosition だけを書く。移動・ホバー・追従の個体差は移動の System が行う。
//       集まってくる演出はしない。タイトルの主役はボスとプレイヤーの構図なので、最初から輪が
//       できている方が絵がまとまる。生成した時点で持ち場に置き、到着済み（hasReachedSlot = true）で始める。
//       TitleScene だけが登録する（REGISTER_* はしない）。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"

#include <SimpleMath.h>
#include <cstdint>

class TitleSwarmSystem
    : public CCL::ECS::IfSystem<TitleSwarmSystem, CCL::ECS::Write<DroneComponent>>
{
    using Base = CCL::ECS::IfSystem<TitleSwarmSystem, CCL::ECS::Write<DroneComponent>>;

public:
    TitleSwarmSystem() : IfSystem("TitleSwarmSystem") {}

    void Update(float dt) override;

    void OnGui() override; // 状態の確認用（ENABLE_EDITOR でないビルドでは何もしない）

    /**
     * @brief テンプレート引数以外に書く型を申告する。
     *
     * 生成した直後に Transform の座標を書くので申告する。
     * テンプレート引数に足すと ForEach の対象まで変わってしまうので、ここで足す。
     */
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override;

private:
    /**
     * @brief ボス像の位置を返す。輪の中心はここで決まるので、像を動かせば輪も付いてくる。
     * @param out ボス像の位置（ワールド）を受け取る。
     * @return 見つかれば true。まだ生成されていなければ false（次のフレームでもう一度探す）。
     */
    bool ResolveBossPos(DirectX::SimpleMath::Vector3 &out);

    // 輪の上の持ち場（本編と同じ式）。回り続けるので毎フレーム変わる
    DirectX::SimpleMath::Vector3 SlotPosition(
        const DirectX::SimpleMath::Vector3 &bossPos, uint16_t index) const;

    // ドローンを生成し、その場で持ち場へ置く
    void SpawnSwarm(const DirectX::SimpleMath::Vector3 &bossPos);

    // 番号から 0〜1 の疑似乱数を作る。起動のたびに同じ絵になるよう、乱数生成器は使わない
    static float Hash01(uint32_t seed);

    static constexpr uint16_t kDroneCount = 50; // 群れの機数

    // 小隊の人数。SlotLag（小隊の後ろの機ほど追従が遅れる）を効かせて、追従の速さに個体差を出す
    static constexpr uint16_t kSquadSize = 7;

    float m_elapsed = 0.0f;  // 輪の回転に使う経過時間（秒）
    bool  m_spawned = false; // ドローンを生成したか

    CCL::ECS::EntityID m_bossID = CCL::ECS::InvalidEntityID; // ボス像。名前で探す
};
