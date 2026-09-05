// 小隊編隊の陣形。全機で1つの形を作る他の技と違い、小隊ごとに独立して動く。
// 基準を SquadState.anchorPos（小隊の頭）へ差し替えるだけで、同じ IFormation の枠の
// まま部隊を何個でも作れる。
//
// ここは持ち場を幾何的に出すだけで遅れは作らない。隊列がしなる表現は移動層が
// slotInSquad に応じてバネを弱めて出す（形の定義と追従の質を別々に調整できる）。
#pragma once
#include "Game/Logics/AI/Drone/Formation/IFormation.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include "Game/Logics/AI/Drone/Squad/SquadState.h"
#include <algorithm>

namespace CCL::AI::Formation::Squad
{
// 小隊突撃（SquadAssault）── 一列縦隊。頭の動きは SquadUpdateSystem が決め、
// ここは「前の機体からどう繋がるか」だけを担当する。
//
//   slot0 ─ slot1 ─ slot2 ─ slot3 ─ slot4
//
// 位置も速度も、基準は必ず“1つ前の機体”であって小隊全体ではない。頭の向きや速度を
// 全機へ配ると、頭が曲がった瞬間に全機が同時に振られて一本の棒が回転するようにしか
// 見えない。前の機体を基準にすると、その基準自体が1リンクごとに遅れて回るので、
// 頭の描いた曲線が胴体を波のように伝わる。
struct SquadAssaultFormation : public IFormation
{
    // 予備動作は「編隊を組んだまま側面へ回り込む」動きそのものなので震えは出さない
    // （読むべき手掛かりが2つになるうえ、震えのずれが後ろの席へ伝播して隊列がガタつく）。
    bool UsesShiverTelegraph() const override { return false; }

    void Calculate(DroneComponent &drone, const FormationContext &ctx) override
    {
        using namespace DirectX::SimpleMath;

        if (ctx.squads == nullptr) return;
        if (drone.squadId >= SquadRegistry::kMaxSquads) return; // 無所属機は動かさない

        const SquadState  &squad  = ctx.squads->squads[drone.squadId];
        const SquadTuning &tuning = ctx.squads->tuning;

        // 突入中もバネで追従させる。剛体にすると隊列が1ミリも歪まず、5機で飛んでも
        // 「1枚の板が近づいてくる」ようにしか見えない。歪むことがこの技の核。
        drone.chargeMode = ChargeMode::SoftFollow;

        const uint8_t maxSlot = static_cast<uint8_t>(SquadRegistry::kSquadSize - 1);
        const uint8_t slot    = (std::min)(drone.slotInSquad, maxSlot);

        Vector3 fwd = squad.anchorForward;
        if (fwd.LengthSquared() < 0.001f) fwd = Vector3::UnitZ;
        fwd.Normalize();

        // 「1つ前の機体」を決める。撃破された席は飛ばして繰り上がるので、欠員が出ても
        // 鎖は切れずに胴体が詰まる。先頭機の“前”は実体を持たない頭（anchor）。
        Vector3 prevPos, prevVel;
        {
            int prev = static_cast<int>(slot) - 1;
            while (prev >= 0 && squad.memberValid[prev] == false) --prev;

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
            drone.targetPosition = squad.anchorPos;  // 先頭機は頭そのものに座る
        }
        else
        {
            // 追従機の目標 = 前の機体の位置 −（前の機体の進行方向）× 間隔。
            Vector3 prevDir = prevVel;
            if (prevDir.LengthSquared() < 0.25f) prevDir = fwd; // ほぼ静止中は頭の向きで代用
            prevDir.Normalize();

            drone.targetPosition = prevPos - prevDir * tuning.chainGap;
        }

        // 床下を指す持ち場は作らない。頭の高度は SquadUpdateSystem が止めているが、
        // 前の機が上向きに抜けた瞬間だけ、その真後ろが床下になる。
        const float floorY = kSquadGroundY + tuning.minAltitude;
        if (drone.targetPosition.y < floorY) drone.targetPosition.y = floorY;

        // 視線は移動側が実際の進行方向で上書きする。ここでは突撃方向の基準としてだけ持つ。
        drone.targetLookDirection = fwd;
        drone.fireDirection       = fwd;

        drone.fireDelay      = 0.0f;  // 小隊技は順次発射を使わない
        drone.attackProgress = 0.0f;
    }
};

inline void RegisterAll()
{
    auto &reg = FormationRegistry::Instance();
    reg.Register<SquadAssaultFormation>(DroneFormationType::SquadAssault);
}

} // namespace CCL::AI::Formation::Squad
