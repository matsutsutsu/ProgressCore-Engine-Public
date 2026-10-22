#pragma once
// ===========================================================================
// 概要: 突撃中（FireCharge）の動き方の基底（IChargeMotion）と、動き方へ渡す調整値。
//
// 責務:
//   - 突撃中の速度と姿勢を決める Move を定める
//   - その間に当たり判定を開けてよいかを返す IsDangerous を定める
//   - 動き方へ毎フレーム渡す調整値（MotionContext）をまとめる
//   - 突撃を始めるときに技へ渡す情報（ChargeBeginInfo）をまとめる
//
// 備考:
//   動き方は ChargeMode 1つにつき1クラス。技（IFormation）が drone.chargeMode を書き、
//   DroneMovementSystem と DroneHitSystem はその値から動き方を引いて呼ぶだけにする。
//   技を足すたびに System へ if を足さずに済ませるため（陣形の IFormation と同じ方式）。
//
//   動き方は複数の技で使い回す（剛体追従はデータ陣形の全技が使う）ので、技のクラスとは分けてある。
//
//   Move は並列ループ（ForEachParallel）から呼ぶので const で書く。
//   動き方が持ってよいのは、BeginFrame（並列ループの前に1回）で写し取った調整値だけ。
// ===========================================================================
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"
#include "Game/Logics/AI/Drone/Components/DroneTuning.h"

namespace CCL::ECS { class World; }

namespace CCL::AI::Motion
{
    // 動き方へ毎フレーム渡す調整値。
    // DroneMovementSystem が Update の先頭で World リソースから1回だけ作る（並列ループの中で resource を引かないため）。
    struct MotionContext
    {
        // 陣形攻撃の予告（震え）の調整値。Idle が使う。
        //
        // 縦割りのように予備動作でほとんど動かない技は、動きで予告できないので合図の前に震えを出す。
        //
        // 弾幕はこれを使わない。動きで予告する技に足すと、予告が二重になって読めなくなる。
        //
        // 振れ幅の単位は m/s ではなく m。速度で揺らすと、実際の幅が「振幅 ÷ 角速度」になる。
        //
        // 震え方は目で見ないと決まらないので、定数にせず World リソースが持つ。
        DroneShiverTuning shiver{};
    };

    // 突撃を始めるときに技へ渡す情報（IFormation::OnChargeBegin）。
    // DroneBrainSystem が Update の先頭で1回だけ作る。
    struct ChargeBeginInfo
    {
        DirectX::SimpleMath::Vector3 playerPos = {};    // プレイヤーの位置（ワールド）。撃つ方向が決まっていないときの狙い
        bool                         hasPlayer = false; // プレイヤーが居るか。居なければ撃つ方向はモデルの前方（-Z）
    };

    // 突撃中（FireCharge）の動き方。ChargeMode 1つにつき1クラス。
    struct IChargeMotion
    {
        virtual ~IChargeMotion() = default;

        /**
         * @brief フレームの最初に1回、並列ループの前に呼ぶ。DroneMovementSystem が呼ぶ。
         *
         * 動き方が自分だけで使う調整値（バネ追従なら小隊のバネの強さ）を World から写し取る口。
         * 並列ループの中で World を引かないため、ここで写して Move では読むだけにする。
         *
         * @param world ドローンが居る World。
         */
        virtual void BeginFrame(CCL::ECS::World &world) {}

        /**
         * @brief 突撃中の速度と姿勢を決める。
         *
         * 書くのは drone.currentVelocity と trans.rotation。位置への積分は DroneMovementSystem が後で行う。
         *
         * @param trans 機体の Transform。
         * @param drone 動かす機体。
         * @param dt    この機体の経過時間 [秒]。
         * @param ctx   動き方へ渡す調整値。
         */
        virtual void Move(TransformComponent &trans, DroneComponent &drone, float dt,
                          const MotionContext &ctx) const = 0;

        /**
         * @brief 突撃中のこの機に、当たり判定を開けてよいかを返す。DroneHitSystem が聞く。
         *
         * FireCharge 以外の状態は、DroneHitSystem が先に除外する。
         * ここで答えるのは、動き方に固有の「当たらない区間」だけ（直進なら発射待ちとタメ）。
         *
         * DroneBrainSystem も「もう飛び出したか」をこれで見る（当たらない間＝まだ飛び出していない）。
         *
         * @param drone 調べる機体。
         * @return 当たり判定を開けてよいなら true。既定は「突撃中はずっと当たる」。
         */
        virtual bool IsDangerous(const DroneComponent &drone) const { return true; }
    };

} // namespace CCL::AI::Motion
