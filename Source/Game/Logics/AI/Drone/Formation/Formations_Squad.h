#pragma once
// ===========================================================================
// 概要: 小隊突撃（SquadAssault）の陣形。小隊ごとに一列縦隊で動く。
//
// 責務:
//   - 各機の持ち場を「1つ前の機体」の位置と速度から出す
//   - 移動方式を SoftFollow にし、1つ前の機体の速度を先渡しする
//   - 持ち場を床より下に置かない
//
// 備考:
//   全機で1つの形を作る他の技と違い、小隊ごとに独立して動く。基準を
//   SquadState.anchorPos（小隊のアンカー）に置くだけで、同じ IFormation のまま
//   小隊をいくつでも作れる。
//   ここは持ち場を幾何的に出すだけで遅れは作らない。隊列がしなる表現は移動層が
//   slotInSquad に応じてバネを弱めて出す（形の定義と追従の質を別々に調整できる）。
// ===========================================================================
#include "Game/Logics/AI/Drone/Formation/IFormation.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include "Game/Logics/AI/Drone/Squad/SquadState.h"
#include <algorithm>

namespace CCL::AI::Formation::Squad
{
    // 小隊突撃（SquadAssault）。一列縦隊。アンカーの動きは SquadUpdateSystem が決め、
    // ここは「前の機体からどう繋がるか」だけを担当する。
    //
    //   slot0 ─ slot1 ─ slot2 ─ slot3 ─ slot4
    //
    // 位置も速度も、基準は必ず「1つ前の機体」であって小隊全体ではない。アンカーの向きや
    // 速度を全機へ配ると、アンカーが曲がった瞬間に全機が同時に振られ、一本の棒が回転する
    // ようにしか見えない。前の機体を基準にすると、その基準自体が1機ごとに遅れて回るので、
    // アンカーの描いた曲線が後続機へ順に伝わる。
    struct SquadAssaultFormation : public IFormation
    {
        // 予備動作は「編隊を組んだまま側面へ回り込む」動きそのものなので震えは出さない
        // （読むべき手掛かりが2つになるうえ、震えのずれが後ろの席へ伝わって隊列が乱れる）。
        bool UsesShiverTelegraph() const override { return false; }

        void Calculate(DroneComponent &drone, const FormationContext &ctx) override
        {
            using namespace DirectX::SimpleMath;

            if (ctx.squads == nullptr) return;
            if (drone.squadId >= SquadRegistry::kMaxSquads) return; // 無所属機は動かさない

            const SquadState  &squad  = ctx.squads->squads[drone.squadId];
            const SquadTuning &tuning = ctx.squads->tuning;

            // 突入中もバネで追従させる。剛体にすると隊列がまったく歪まず、5機で飛んでも
            // 「1枚の板が近づいてくる」ようにしか見えない。歪むことがこの技の要点。
            drone.chargeMode = ChargeMode::SoftFollow;

            const uint8_t maxSlot = static_cast<uint8_t>(SquadRegistry::kSquadSize - 1);
            const uint8_t slot    = (std::min)(drone.slotInSquad, maxSlot);

            Vector3 fwd = squad.anchorForward;
            if (fwd.LengthSquared() < 0.001f) fwd = Vector3::UnitZ;
            fwd.Normalize();

            // 「1つ前の機体」を決める。撃破された席は飛ばして繰り上がるので、欠員が出ても
            // 隊列は途切れずに詰まる。先頭機の「前」は実体を持たないアンカー。
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

            drone.followFeedForward = prevVel;

            if (slot == 0)
            {
                drone.targetPosition = squad.anchorPos; // 先頭機はアンカーの位置に置く
            }
            else
            {
                // 追従機の目標 = 前の機体の位置 −（前の機体の進行方向）× 間隔。
                Vector3 prevDir = prevVel;
                if (prevDir.LengthSquared() < 0.25f) prevDir = fwd; // ほぼ静止中はアンカーの向きで代用
                prevDir.Normalize();

                drone.targetPosition = prevPos - prevDir * tuning.chainGap;
            }

            // 床下を指す持ち場は作らない。アンカーの高度は SquadUpdateSystem が止めているが、
            // 前の機が上向きに抜けた瞬間だけ、その真後ろが床下になる。
            const float floorY = kSquadGroundY + tuning.minAltitude;
            if (drone.targetPosition.y < floorY) drone.targetPosition.y = floorY;

            // 視線は移動側が実際の進行方向で上書きする。ここでは突撃方向の基準としてだけ持つ。
            drone.targetLookDirection = fwd;
            drone.fireDirection       = fwd;

            drone.fireDelay      = 0.0f; // 小隊技は順次発射を使わない
            drone.attackProgress = 0.0f;
        }
    };

    inline void RegisterAll()
    {
        auto &reg = FormationRegistry::Instance();
        reg.Register<SquadAssaultFormation>(DroneFormationType::SquadAssault);
    }

} // namespace CCL::AI::Formation::Squad
