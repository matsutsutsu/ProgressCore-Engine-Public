/**
 * 小隊編隊の陣形。全機で1つの形を作る他の技と違い、小隊ごとに独立して動く。
 *
 * 他の陣形との違い:
 *   既存の陣形は localIndex / totalDrones（群れ全体の何番目か）から座標を決め、
 *   基準はボスの位置だった。つまり「1つの巨大な図形」しか作れない。
 *   小隊陣形は基準を SquadState.anchorPos（小隊の頭）に差し替えるだけで、
 *   同じ IFormation の枠のまま「独立して動く部隊」が何個も作れる。
 *
 * 追従の遅れについて:
 *   ここでは全機の持ち場を「頭の現在の姿勢」から幾何的に出すだけで、
 *   遅れは作らない。後方の機がワンテンポ遅れて曲がる（隊列がしなる）表現は
 *   DroneMovementSystem 側で slotInSquad に応じてバネを弱めることで出している。
 *   形の定義と追従の質を別々の層に置くと、片方だけを調整できる。
 */
#pragma once
#include "Game/Logics/AI/Drone/Formation/IFormation.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include "Game/Logics/AI/Drone/Squad/SquadState.h"
#include <algorithm>

namespace CCL::AI::Formation::Squad
{
// ====================================================================
// 小隊突撃（SquadAssault）── 一列縦隊（蛇）
//   先頭1機に4機が数珠つなぎで付いていく。頭の動きは SquadUpdateSystem が決め、
//   ここは「前の機体からどう繋がるか」だけを担当する。
//
//   slot0 ─ slot1 ─ slot2 ─ slot3 ─ slot4
//
//   各機は「頭からの固定位置」ではなく「1つ前の機体の真後ろ」に付く。
//   位置も速度も、基準は必ず“1つ前の機体”であって小隊全体ではない。
//   頭の向きや頭の速度を全機へ配ると、頭が曲がった瞬間に全機が同時に振られ、
//   一本の棒が回転するようにしか見えない（＝横線が迫ってくる）。
//   前の機体を基準にすると、その基準自体が1リンクごとに遅れて回るので、
//   頭の描いた曲線が胴体を波のように伝わる。
// ====================================================================
struct SquadAssaultFormation : public IFormation
{
    void Calculate(DroneComponent &drone, const FormationContext &ctx) override
    {
        using namespace DirectX::SimpleMath;

        if (ctx.squads == nullptr) return;
        if (drone.squadId >= SquadRegistry::kMaxSquads) return; // 無所属機は動かさない

        const SquadState  &squad  = ctx.squads->squads[drone.squadId];
        const SquadTuning &tuning = ctx.squads->tuning;

        // 隊列を組んだまま飛ぶ技なので、突入中も「バネで」追従させる。
        //   剛体（RigidFollow）にすると隊列が1ミリも歪まず、5機で飛んでも
        //   「1枚の板が近づいてくる」ようにしか見えない。歪むことがこの技の核。
        drone.chargeMode = ChargeMode::SoftFollow;

        const uint8_t maxSlot = static_cast<uint8_t>(SquadRegistry::kSquadSize - 1);
        const uint8_t slot    = (std::min)(drone.slotInSquad, maxSlot);

        Vector3 fwd = squad.anchorForward;
        if (fwd.LengthSquared() < 0.001f) fwd = Vector3::UnitZ;
        fwd.Normalize();

        // ------------------------------------------------------------
        // 「1つ前の機体」を決める。撃破された席は飛ばして繰り上がるので、
        // 欠員が出ても鎖は切れずに胴体が詰まる。
        // 先頭機（slot 0）の“前”は実体を持たない頭（anchor）。
        // ------------------------------------------------------------
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

        // 速度の継承。
        //   ここで小隊全体の速度（anchorVelocity）を全機に配ってはいけない。
        //   突入中の anchorVelocity はプレイヤーへ向かう 40m/s 級のベクトルなので、
        //   それを全機に配ると全機が同じ方向へ同じ速さで飛ぶ。バネの補正は数m/sしか
        //   出ないので完全に埋もれ、「各自がプレイヤーへ突っ込む」絵になる。
        //   前の機体の速度だけを引き継ぐと、頭が曲がってから胴体へ順に伝わる。
        drone.followFeedForward = prevVel;

        if (slot == 0)
        {
            // 先頭機は頭（anchor）そのものに座る
            drone.targetPosition = squad.anchorPos;
        }
        else
        {
            // --------------------------------------------------------
            // 追従機：1つ前の機体の「進行方向の真後ろ」に付く
            //
            //   目標 = 前の機体の位置 −（前の機体の進行方向）× 間隔
            //
            //   基準を頭の向き（anchorForward）にしないのが要点。
            //   頭の向きを全機で共有すると、頭が曲がった瞬間に胴体のオフセットが
            //   一斉に回り、一本の棒が回転するようにしか見えない（＝横線が迫る）。
            //   前の機体“それぞれ”の進行方向を基準にすると、その向き自体が
            //   1リンクごとに遅れて回るので、頭の描いた曲線が波のように後ろへ伝わる。
            // --------------------------------------------------------
            Vector3 prevDir = prevVel;
            if (prevDir.LengthSquared() < 0.25f) prevDir = fwd; // ほぼ静止中は頭の向きで代用
            prevDir.Normalize();

            drone.targetPosition = prevPos - prevDir * tuning.chainGap;
        }

        // 視線は移動側が「実際に進んでいる方向」で上書きする。
        // ここでは突撃方向の基準としてだけ持たせる。
        drone.targetLookDirection = fwd;
        drone.fireDirection       = fwd;

        // 小隊技では順次発射を使わない（全機が数珠つなぎのまま動く）
        drone.fireDelay      = 0.0f;
        drone.attackProgress = 0.0f;
    }
};

// ====================================================================
// 登録
// ====================================================================
inline void RegisterAll()
{
    auto &reg = FormationRegistry::Instance();
    reg.Register<SquadAssaultFormation>(DroneFormationType::SquadAssault);
}

} // namespace CCL::AI::Formation::Squad
