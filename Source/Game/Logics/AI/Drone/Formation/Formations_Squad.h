#pragma once
// ===========================================================================
// 概要: 小隊突撃（SquadAssault）の陣形。小隊ごとに一列縦隊で動く
//
// 責務:
//   - 各機の持ち場を「1つ前の機体」の位置と速度から出す
//   - 移動方式を SoftFollow にし、1つ前の機体の速度を先渡しする
//   - 後ろの席ほど持ち場へ寄るバネを弱める（springScale）
//   - 持ち場を床より下に置かない
//
// 備考:
//   基準をボスではなく小隊のアンカー（SquadState.anchorPos）に置くので、小隊ごとに独立して動く
//   アンカーの動きは SquadUpdateSystem が決め、ここは「前の機体からどう繋がるか」だけを持つ
// ===========================================================================
#include "Game/Logics/AI/Drone/Formation/IFormation.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include "Game/Logics/AI/Drone/Squad/SquadState.h"
#include "Engine/ECS/Core/CCL_World.h"
#include <algorithm>

namespace CCL::AI::Formation::Squad
{
    /**
     * @brief 小隊突撃（SquadAssault）。一列縦隊（slot0 ─ slot1 ─ … ─ slot4）
     *
     * 位置も速度も、基準は「1つ前の機体」で小隊全体ではない
     * アンカーの向きを全機へ配ると、曲がった瞬間に一本の棒が回るようにしか見えない
     * 前の機体を基準にすれば、アンカーの描いた曲線が後続機へ順に伝わる
     */
    class SquadAssaultFormation : public IFormation
    {
    public:
        // 頭上から降りてくる動きが予告。震えを足すと、そのずれが後ろの席へ伝わって隊列が乱れる
        bool UsesShiverTelegraph() const override { return false; }

        bool FollowsBoss() const override { return false; } // 基準はアンカー

        // 追跡中は判定を開けたまま往復するので、何度も当てる
        // 間隔は1回すり抜ける時間より長くし、1回の通過で2度当てない
        FormationCombat GetCombat() const override
        {
            FormationCombat c;
            c.rehitInterval = 1.0f;
            return c;
        }

        // 追跡は戻すまで続く。普段は BT の Parallel の主（滞空）が先に終わり、技ごと打ち切られる
        bool TryGetTiming(TechniqueTiming &out) const override
        {
            out = {2.4f, 20.0f, 0.0f};
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
            // 1. 移動方式をバネ追従にし、後ろの席ほどバネを弱める
            // 2. 「1つ前の機体」を決める
            // 3. 持ち場と先渡しの速度を決める
            // 4. 持ち場を床より下に置かない
            // 5. 向きを決め、他の技の値を戻す

            if (m_squads == nullptr) return;
            if (drone.squadId >= SquadRegistry::kMaxSquads) return; // 無所属機は動かさない

            const SquadState  &squad  = m_squads->squads[drone.squadId];
            const SquadTuning &tuning = m_squads->tuning;

            // --- 移動方式 ---

            // 剛体にすると隊列が歪まず、1枚の板が近づいてくるようにしか見えない
            drone.chargeMode = ChargeMode::SoftFollow;

            // 後ろの席ほど遅れて曲がり、隊列が先頭から順にしなる
            drone.springScale = 1.0f - static_cast<float>(drone.slotInSquad) * kSpringLagPerSlot;

            const uint8_t maxSlot = static_cast<uint8_t>(SquadRegistry::kSquadSize - 1);
            const uint8_t slot    = (std::min)(drone.slotInSquad, maxSlot);

            Vector3 fwd = squad.anchorForward;
            if (fwd.LengthSquared() < 0.001f) fwd = Vector3::UnitZ;
            fwd.Normalize();

            // --- 1つ前の機体 ---

            // 撃破された席は飛ばす（欠員が出ても隊列が詰まる）。先頭機の「前」はアンカー
            Vector3 prevPos, prevVel;
            {
                int prev = static_cast<int>(slot) - 1;
                while (prev >= 0 && squad.memberValid[prev] == false)
                    --prev;

                if (prev < 0)
                {
                    prevPos = squad.anchorPos;
                    prevVel = squad.anchorVelocity;
                }
                else
                {
                    prevPos = squad.memberPos[prev];
                    prevVel = squad.memberVel[prev];
                }
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

            // 追跡中（SoftFollow）は、移動層が実際の進行方向へ向け直す
            drone.targetLookDirection = fwd;
            drone.fireDirection       = fwd;

            drone.attackProgress = 0.0f;
        }

    private:
        static constexpr float kSpringLagPerSlot = 0.06f; // 1つ後ろの席ごとにバネを弱める割合

        const SquadRegistry *m_squads = nullptr; // BeginFrame で取った小隊の状態。なければ nullptr
    };

    inline void RegisterAll(FormationRegistry &reg)
    {
        reg.Register<SquadAssaultFormation>(DroneFormationType::SquadAssault);
    }

}
