#pragma once
// ===========================================================================
// 概要: 小隊（5機編隊）のアンカーを動かす System。小隊単位の判断はここに集める
//
// 責務:
//   - 各席の前フレームの位置・速度を集める
//   - 小隊ごとに段階（Idle / FormFan / Arc / Return）を決め、アンカーを動かす
//   - 通し番号から、機体の小隊と席を決める
//
// 備考: 各機の持ち場は、アンカーを起点に陣形（Formations_Squad.h）が出す
//       機体ごとのループで行うと、同じ小隊の計算を5回繰り返し、同じ小隊へ並列に書き込む
//       そこで小隊単位の System にし、単スレッドで回す
// ===========================================================================
#include <vector>

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"
#include "Game/Logics/AI/Drone/Squad/SquadState.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"

class SquadUpdateSystem : public CCL::ECS::IfSystem<
    SquadUpdateSystem,
    CCL::ECS::Read<DroneComponent>,
    CCL::ECS::Read<TransformComponent>>
{
    using Base = CCL::ECS::IfSystem<
        SquadUpdateSystem,
        CCL::ECS::Read<DroneComponent>,
        CCL::ECS::Read<TransformComponent>>;

public:
    SquadUpdateSystem() : IfSystem("SquadUpdateSystem") {}

    void Initialize() override;

    // 命令と、生成時に振った小隊の席を読んでから動く
    // BehaviorTreeSystem は間接的に後ろになるが、名指ししないと起動時に順序不定と報告される
    std::vector<const char *> GetRunAfter() const override
    {
        return {"BehaviorTreeSystem", "DroneCommandSystem", "DroneManagerSystem"};
    }

    std::vector<CCL::ECS::TypeID> GetReadTypes() const override
    {
        auto types = Base::GetReadTypes();
        types.push_back(CCL::ECS::TypeInfo<BossCommandComponent>::ID()); // テンプレート引数に足すと ForEach の絞り込みにもなる
        return types;
    }

    void Update(float dt) override;

    void OnGui() override;

    // 通し番号（localIndex）から、この機の小隊と席を決める。DroneManagerSystem が番号を振り直すたびに呼ぶ
    static void AssignSeat(DroneComponent &drone);

private:
    // 全小隊に共通の、このフレームの入力。Update の先頭で1回だけ集める
    struct SquadFrame
    {
        bool                         ordered     = false; // 弧撃ちが指示されていて、ボスとプレイヤーもいる
        bool                         attack      = false; // ボスの「今撃て」信号
        int                          launchCount = 0;     // 飛ばす小隊の数（いる小隊の数を超えない）
        DirectX::SimpleMath::Vector3 bossPos     = DirectX::SimpleMath::Vector3::Zero;  // ボスの位置（ワールド）
        DirectX::SimpleMath::Vector3 playerPos   = DirectX::SimpleMath::Vector3::Zero;  // プレイヤーの体の中心（ワールド）
        DirectX::SimpleMath::Vector3 aimDir      = DirectX::SimpleMath::Vector3::UnitZ; // ボスからプレイヤーへの水平の向き
        float                        dt          = 0.0f;  // 敵の時間で進めた経過時間 [秒]
    };

    // ============================================================
    // 入力の収集
    // ============================================================

    // 各席の位置・速度を集め直し、機体のいる小隊の数を返す
    // Movement（L03）より前に走るので、集まるのは前フレームの位置
    int GatherMembers(SquadRegistry &reg);

    // 時間・ボスの命令・ボスとプレイヤーの位置を読み、このフレームの入力にまとめる
    SquadFrame ReadFrame(float rawDt, int squadCount, const SquadTuning &tuning) const;

    // ============================================================
    // アンカーの動き
    // ============================================================

    // 小隊1つの段階を決めてアンカーを動かす
    void UpdateSquad(SquadState &squad, uint8_t squadId, const SquadFrame &frame, const SquadTuning &tuning) const;

    // 次の段階を決める。番号の若い小隊から launchCount 個だけを扇へ並べ、撃つ合図で飛ばし、飛び終わったら戻す
    // squad.phase はまだ前フレームの段階のまま読む
    SquadPhase ChoosePhase(const SquadState &squad, uint8_t squadId, const SquadFrame &frame) const;

    // アンカーを先頭の機に重ねておく（Idle と Return。機体は陣形が頭上円陣の席へ寄せる）
    // 動き出した瞬間に、アンカーが原点からでなく機体の今いる場所から動き出す
    // アンカーの速度は 0 にする。先頭の機の速度を渡すと、席0の機が止まらずに流れ続ける
    void FollowLeadMember(SquadState &squad) const;

    // 扇へ並ぶ。入った瞬間の位置から、扇の位置まで fanFormTime 秒かけて動き、並んだらそこに留まる
    void UpdateFormFan(SquadState &squad, uint8_t squadId, SquadPhase prevPhase, const SquadFrame &frame,
                       const SquadTuning &tuning) const;

    // この小隊が並ぶ扇の位置（ワールド）を返す
    DirectX::SimpleMath::Vector3 FanPosition(uint8_t squadId, const SquadFrame &frame, const SquadTuning &tuning) const;

    // この小隊の扇の上の角度 [rad] を返す。0 が真上で、プレイヤーから見て右が正
    float FanAngle(uint8_t squadId, const SquadFrame &frame, const SquadTuning &tuning) const;

    // 弧を描いて飛ぶ。入った瞬間に道筋を決め、一定の速さで進める
    // 道筋の終わりから先は最後の向きのまま直進し、最後の機が終点を越えたら ChoosePhase が Return にする
    void UpdateArc(SquadState &squad, uint8_t squadId, SquadPhase prevPhase, const SquadFrame &frame,
                   const SquadTuning &tuning) const;

    // 最後の席の機が終点（撃った瞬間のプレイヤー位置）を越えたかを返す
    bool HasTailPassed(const SquadState &squad) const;

    // 撃った瞬間の位置から、弧の道筋（始点・曲がり角・終点・通り過ぎた先）を決める
    void BeginArc(SquadState &squad, uint8_t squadId, const SquadFrame &frame, const SquadTuning &tuning) const;

    // 道筋の進み具合 progress の位置を返し、その点の接線（進み具合1あたりの移動量）を outTangent に書く
    DirectX::SimpleMath::Vector3 SampleArc(const SquadState &squad, float progress,
                                           DirectX::SimpleMath::Vector3 &outTangent) const;

    // アンカーの向きを face の方へ少しずつ寄せる。face がほぼ 0 なら変えない
    void TurnAnchorToward(SquadState &squad, DirectX::SimpleMath::Vector3 face, float dt) const;

    // ============================================================
    // デバッグ UI
    // ============================================================

    // 小隊の調整値をスライダーで出す
    void DrawTuning(SquadTuning &tuning) const;
};
