#pragma once
// ===========================================================================
// 概要: 小隊突撃（SquadAssault）の陣形。小隊ごとに一列縦隊で動く。
//
// 責務:
//   - 各機の持ち場を「1つ前の機体」の位置と速度から出す
//   - 移動方式を SoftFollow にし、1つ前の機体の速度を先渡しする
//   - 後ろの席ほど持ち場へ寄るバネを弱める（springScale）
//   - 持ち場を床より下に置かない
//
// 備考:
//   全機で1つの形を作る他の技と違い、小隊ごとに独立して動く。基準を
//   SquadState.anchorPos（小隊のアンカー）に置くだけで、同じ IFormation のまま
//   小隊をいくつでも作れる。
//
//   ここは持ち場を幾何的に出すだけで、遅れそのものは作らない。隊列がしなる表現は、
//   ここが書いた springScale を移動層がバネの強さに掛けて出す（形の定義と追従の質を別々に調整できる）。
//
//   小隊の状態（SquadRegistry）は BeginFrame で自分で取りに行く。共通の FormationContext には載せない。
// ===========================================================================
#include "Game/Logics/AI/Drone/Formation/IFormation.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include "Game/Logics/AI/Drone/Squad/SquadState.h"
#include "Engine/ECS/Core/CCL_World.h"
#include <algorithm>

namespace CCL::AI::Formation::Squad
{
    // 小隊突撃（SquadAssault）。一列縦隊。アンカーの動きは SquadUpdateSystem が決め、
    // ここは「前の機体からどう繋がるか」だけを担当する。
    //
    //   slot0 ─ slot1 ─ slot2 ─ slot3 ─ slot4
    //
    // 位置も速度も、基準は必ず「1つ前の機体」であって小隊全体ではない。
    //
    // アンカーの向きや速度を全機へ配ると、アンカーが曲がった瞬間に全機が同時に振られ、
    // 一本の棒が回転するようにしか見えない。
    //
    // 前の機体を基準にすると、その基準自体が1機ごとに遅れて回るので、
    // アンカーの描いた曲線が後続機へ順に伝わる。
    struct SquadAssaultFormation : public IFormation
    {
        // 予備動作は「編隊を組んだままボスの周りへ集まり、降りてくる」動きそのものなので、震えは出さない。
        // 読むべき手掛かりが2つになるうえ、震えのずれが後ろの席へ伝わって隊列が乱れる。
        bool UsesShiverTelegraph() const override { return false; }

        // 基準は先頭機（SquadUpdateSystem が動かすアンカー）で、ボスではない。
        // 先渡しの速度も自分で「1つ前の機体の速度」を入れる。
        bool FollowsBoss() const override { return false; }

        // 追跡中は判定を開けたまま何度も往復するので、1回きりだと2周目から当たらない。

        // 間隔は1回すり抜けるのにかかる時間より長く取る。1回の通過で2度当たらないようにするため。
        FormationCombat GetCombat() const override
        {
            FormationCombat c;
            c.rehitInterval = 1.0f;
            return c;
        }

        // 小隊の状態を取りに行く。小隊の係（SquadUpdateSystem）がこのフレームの分を更新済み。
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

            // 追跡中もバネで追従させる。
            // 剛体にすると隊列がまったく歪まず、5機で飛んでも「1枚の板が近づいてくる」ようにしか見えない。
            // 歪むことがこの技の要点。
            drone.chargeMode = ChargeMode::SoftFollow;

            // 後ろの席ほどバネを弱める。後方の機ほど遅れて曲がるので、隊列が先頭から順に曲がっていく。
            // 編隊で飛んでいるように見えるのは主にこの遅れによる。
            drone.springScale = 1.0f - static_cast<float>(drone.slotInSquad) * kSpringLagPerSlot;

            const uint8_t maxSlot = static_cast<uint8_t>(SquadRegistry::kSquadSize - 1);
            const uint8_t slot    = (std::min)(drone.slotInSquad, maxSlot);

            Vector3 fwd = squad.anchorForward;
            if (fwd.LengthSquared() < 0.001f) fwd = Vector3::UnitZ;
            fwd.Normalize();

            // --- 1つ前の機体 ---

            // 撃破された席は飛ばして繰り上がる。欠員が出ても隊列は途切れずに詰まる。

            // 先頭機の「前」は、実体を持たないアンカー。
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

            // 先渡しの速度も「1つ前の機体」の速度にする（小隊全体の速度ではない。理由はこの構造体の上の説明）。
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

            // --- 床 ---

            // 床下を指す持ち場は作らない。
            // アンカーの高度は SquadUpdateSystem が止めているが、前の機が上向きに抜けた瞬間だけ、
            // その真後ろが床下になる。
            if (drone.targetPosition.y < kSquadFloorY) drone.targetPosition.y = kSquadFloorY;

            // --- 向きと他の技の値 ---

            // 機首はアンカーの向きを基準にする。
            // 追跡中（SoftFollow）は、移動層が実際の進行方向へ向け直す。
            drone.targetLookDirection = fwd;
            drone.fireDirection       = fwd;

            drone.attackProgress = 0.0f;
        }

    private:
        // 1つ後ろの席ごとにバネを弱める割合。最後尾（席4）で 0.76 倍になる。
        static constexpr float kSpringLagPerSlot = 0.06f;

        const SquadRegistry *m_squads = nullptr; // BeginFrame で取った小隊の状態。無ければ nullptr
    };

    inline void RegisterAll(FormationRegistry &reg)
    {
        reg.Register<SquadAssaultFormation>(DroneFormationType::SquadAssault);
    }

} // namespace CCL::AI::Formation::Squad
