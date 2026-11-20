#pragma once
// ===========================================================================
// 概要: 小隊の弧撃ち（SquadArc）の陣形。小隊ごとに一列縦隊で動く
//
// 責務:
//   - 飛ばない機（円陣で待つ小隊・飛び終わった小隊・無所属）を頭上円陣の席に置く
//   - 弧を飛んでいる小隊の機だけを当たるようにする
//   - 技の時間割を、小隊の調整値（並ぶ・構える・戻すまで）から出す
//   - 飛ぶ小隊の各機の持ち場を「1つ前の機体」の位置と速度から出す
//   - 移動方式を SoftFollow にし、1つ前の機体の速度を先渡しする
//   - 後ろの席ほど持ち場へ寄るバネを弱める（springScale）
//   - 持ち場を床より下に置かない
//
// 備考:
//   飛ぶ小隊は、基準をボスではなく小隊のアンカー（SquadState.anchorPos）に置くので、小隊ごとに独立して動く
//   アンカーの動きは SquadUpdateSystem が決め、ここは「前の機体からどう繋がるか」だけを持つ
// ===========================================================================
#include "Game/Logics/AI/Drone/Formation/IFormation.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include "Game/Logics/AI/Drone/Formation/Formations_Basic.h"
#include "Game/Logics/AI/Drone/Squad/SquadState.h"
#include "Engine/ECS/Core/CCL_World.h"
#include <algorithm>

namespace CCL::AI::Formation::Squad
{
    /**
     * @brief 小隊の弧撃ち（SquadArc）。一列縦隊（slot0 ─ slot1 ─ … ─ slot4）
     *
     * 位置も速度も、基準は「1つ前の機体」で小隊全体ではない
     * アンカーの向きを全機へ配ると、曲がった瞬間に一本の棒が回るようにしか見えない
     * 前の機体を基準にすれば、アンカーの描いた曲線が後続機へ順に伝わる
     */
    class SquadArcFormation : public IFormation
    {
    public:
        // 震えを足すと、そのずれが後ろの席へ伝わって隊列が乱れる
        bool UsesShiverTelegraph() const override { return false; }

        // 飛ばない機は頭上円陣にいるので、円陣と同じくボスの速度を先渡しし、跳躍中もボスと一緒に上下する
        // 飛ぶ小隊は、Calculate で先渡しの速度を1つ前の機体の速度に上書きする
        bool FollowsBoss() const override { return true; }
        bool FollowsBossHeight() const override { return true; }

        // 剛体にすると隊列が歪まず、1枚の板が近づいてくるようにしか見えない
        ChargeMode GetChargeMode() const override { return ChargeMode::SoftFollow; }

        // 当たり方は既定のまま。1回の技で各小隊は1回しか通らないので、再ヒット間隔も既定（突撃1回につき1ヒット）にする

        // 撃つ合図は全機へ一斉に出るので、円陣で待つ機も突撃（FireCharge）に入る
        // 当たるのは弧を飛んでいる小隊の機だけにし、待つ機・戻る機・無所属の機は閉じる
        // DroneHitSystem（L03）から呼ばれる。小隊の状態を書く SquadUpdateSystem は L02 で書き終えている
        bool IsHitWindowOver(const DroneComponent &drone) const override
        {
            if (m_squads == nullptr) return true;
            if (drone.squadId >= SquadRegistry::kMaxSquads) return true;
            return m_squads->squads[drone.squadId].phase != SquadPhase::Arc;
        }

        // 予告は「扇へ並ぶ＋構える」。撃ってから戻すまでは調整値のまま（弧の長さはプレイヤーとの距離で変わり、前もって出せない）
        // BT が毎フレーム呼ぶ。最初のフレームはまだ BeginFrame が来ていないので、既定の調整値で出す
        bool TryGetTiming(TechniqueTiming &out) const override
        {
            const SquadTuning tuning = m_squads ? m_squads->tuning : SquadTuning{};

            out.telegraph = tuning.fanFormTime + tuning.fanHoldTime;
            out.recovery  = tuning.recoveryTime;
            out.rest      = 0.0f;
            return true;
        }

        // SquadUpdateSystem がこのフレームの分を更新済み
        void BeginFrame(CCL::ECS::World &world) override
        {
            m_squads = world.HasResource<SquadRegistry>() ? &world.GetResource<SquadRegistry>() : nullptr;
        }

        void Calculate(DroneComponent &drone, const FormationContext &ctx) override
        {
            using namespace DirectX::SimpleMath;

            // 処理フロー:
            // 1. 飛んでいない機は頭上円陣の席に置いて終える
            // 2. 後ろの席ほどバネを弱める
            // 3. 「1つ前の機体」を決める
            // 4. 持ち場と先渡しの速度を決める
            // 5. 持ち場を床より下に置かない
            // 6. 向きを決め、他の技の値を戻す

            if (m_squads == nullptr) return;

            // --- 飛んでいない機 ---

            // 円陣で待つ小隊と無所属の機は、頭上円陣と同じ席に置く。飛んだ小隊の席だけが空いて見える
            // 飛び終わった小隊（Return）も同じ席を持ち場にする。各機がバネで自分の席へ戻り、空いた席が埋まる
            const bool inSquad = drone.squadId < SquadRegistry::kMaxSquads;
            if (!inSquad)
            {
                m_orbit.Calculate(drone, ctx);
                return;
            }

            const SquadPhase phase = m_squads->squads[drone.squadId].phase;
            if (phase == SquadPhase::Idle || phase == SquadPhase::Return)
            {
                m_orbit.Calculate(drone, ctx);
                return;
            }

            const SquadState  &squad  = m_squads->squads[drone.squadId];
            const SquadTuning &tuning = m_squads->tuning;

            // --- バネの強さ ---

            // 後ろの席ほど遅れて曲がり、隊列が先頭から順にしなる
            drone.springScale = 1.0f - static_cast<float>(drone.slotInSquad) * kSpringLagPerSlot;

            const uint8_t maxSlot = static_cast<uint8_t>(SquadRegistry::kSquadSize - 1);
            const uint8_t slot    = (std::min)(drone.slotInSquad, maxSlot);

            Vector3 fwd = squad.anchorForward;
            if (fwd.LengthSquared() < 0.001f) fwd = Vector3::UnitZ;
            fwd.Normalize();

            // --- 1つ前の機体 ---

            // 先頭機の「前」はアンカー。ドローンは撃破されないので、1つ前の席には必ず機体がいる
            Vector3 prevPos = squad.anchorPos;
            Vector3 prevVel = squad.anchorVelocity;
            if (slot > 0)
            {
                prevPos = squad.memberPos[slot - 1];
                prevVel = squad.memberVel[slot - 1];
            }

            // --- 持ち場と先渡しの速度 ---

            drone.followFeedForward = prevVel;

            if (slot == 0)
            {
                drone.targetPosition = squad.anchorPos;
            }
            else
            {
                // 前の機体の位置から、その進行方向の後ろへ間隔ぶん下げる
                Vector3 prevDir = prevVel;
                if (prevDir.LengthSquared() < 0.25f) prevDir = fwd; // ほぼ静止中はアンカーの向きで代用
                prevDir.Normalize();

                drone.targetPosition = prevPos - prevDir * tuning.chainGap;
            }

            // --- 床 ---

            // アンカーの高度は止めてあるが、前の機が上向きに抜けた瞬間だけ、その真後ろが床下になる
            if (drone.targetPosition.y < kSquadFloorY) drone.targetPosition.y = kSquadFloorY;

            // --- 向きと他の技の値 ---

            // 突撃中（SoftFollow）は、移動層が実際の進行方向へ向け直す
            drone.targetLookDirection = fwd;
            drone.fireDirection       = fwd;

            drone.attackProgress = 0.0f;
        }

    private:
        static constexpr float kSpringLagPerSlot = 0.06f; // 1つ後ろの席ごとにバネを弱める割合

        const SquadRegistry *m_squads = nullptr; // BeginFrame で取った小隊の状態。なければ nullptr

        Basic::HighOrbitFormation m_orbit; // 飛ばない機の席を出す。状態を持たないので、登録済みの実体を引かずに自分で持つ
    };

    inline void RegisterAll(FormationRegistry &reg)
    {
        reg.Register<SquadArcFormation>(DroneFormationType::SquadArc);
    }

}
