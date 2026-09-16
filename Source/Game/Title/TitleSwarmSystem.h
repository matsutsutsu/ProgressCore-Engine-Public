#pragma once
/// タイトル画面の群れ。ドローンを自前で生成し、ボス像の頭上で
/// ゲーム本編と同じ頭上円陣（HighOrbit）に並べる。
///
/// なぜ本編の式をそのまま呼ぶか:
///   半径・高さ・回転速度は `Formations_Basic.h` の `HighRing` が持っている。
///   タイトル用に数値をコピーすると、本編を調整したときに片方だけ古くなり、
///   「タイトルで見た輪」と「ゲームで見る輪」が別物になる。
///   ここは HighRing::SlotOffset() を呼ぶだけにして、広さは本編に従う。
///
/// なぜ陣形(IFormation)を通さないか:
///   陣形の計算は DroneFormationSystem がボスの命令(BossCommand)から回す。
///   タイトルにはボスの実体（BT・命令）が無いので陣形は回らない。
///   そこで currentFormation=Hidden で陣形計算を素通りさせ、
///   同じ HighRing の式で targetPosition だけを直接書く。
///   移動・ホバー・追従の個体差は既存の移動層がそのまま担う。
///
/// なぜ集合演出をしないか:
///   散開状態から1機ずつ合流させる版も作ったが、タイトルの主役はボスと
///   プレイヤーの構図なので、最初から完成した輪が出ているほうが絵が締まる。
///   生成した瞬間に持ち場へ置き、到着済み(hasReachedSlot=true)で始める。
///
/// TitleScene だけが登録する（REGISTER_* はしない）。
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"

#include <SimpleMath.h>
#include <cstdint>

class TitleSwarmSystem
    : public CCL::ECS::IfSystem<TitleSwarmSystem, CCL::ECS::Write<DroneComponent>>
{
    using Base = CCL::ECS::IfSystem<TitleSwarmSystem, CCL::ECS::Write<DroneComponent>>;

public:
    TitleSwarmSystem() : IfSystem("TitleSwarmSystem") {}

    void Update(float dt) override;

    void OnGui() override; // 状態の確認用（Shipping では空実装）

    /// 生成直後の座標書き込みで Transform を触るので申告する。
    /// テンプレート引数には足せない（ForEach の絞り込みまで変わってしまう）。
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override;

private:
    /// ボス像の位置。輪の中心をここから決めるので、像を動かせば輪も付いてくる。
    /// まだ生成されていなければ false（次フレームに再試行）。
    bool ResolveBossPos(DirectX::SimpleMath::Vector3 &out);

    /// 環上の持ち場（本編と同じ式）。回り続けるので毎フレーム変わる。
    DirectX::SimpleMath::Vector3 SlotPosition(
        const DirectX::SimpleMath::Vector3 &bossPos, uint16_t index) const;

    /// ドローンを生成し、その場で持ち場へ置く。
    void SpawnSwarm(const DirectX::SimpleMath::Vector3 &bossPos);

    /// 番号から 0..1 の疑似乱数。毎回同じ絵になるよう乱数生成器は使わない
    /// （作品提出物なので、起動ごとに揺れ方が変わるのは扱いにくい）。
    static float Hash01(uint32_t seed);

    /// 群れの機数。全機が頭上円陣に並ぶ。
    static constexpr uint16_t kDroneCount = 50;

    /// 小隊の人数。既存の SlotLag（後ろの機ほど追従が鈍る）へ効かせて
    /// 追従の速さに個体差を出すために振る。
    static constexpr uint16_t kSquadSize = 7;

    float m_elapsed = 0.0f;
    bool  m_spawned = false;

    CCL::ECS::EntityID m_bossID = CCL::ECS::InvalidEntityID; // ボス像。名前で引く
};
