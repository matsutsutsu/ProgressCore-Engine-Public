#pragma once
// ===========================================================================
// 概要: 小隊（5機編隊）のアンカーを動かす System。小隊単位の判断はここに集める
//
// 責務:
//   - 各小隊の生存数と、各席の前フレームの位置・速度を集める
//   - 段階（Idle / Descend / Chase）を決め、アンカーを動かす
//   - 生成された機を、小隊の空き席へ入れる
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

namespace CCL::ECS { class World; }

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

    // まだ小隊に入っていない機を、空いている席へ入れる。DroneManagerSystem が生成の直後に呼ぶ
    // 所属は一度決めたら変えない。振り直すと生き残った機の所属まで入れ替わり、同じ部隊に見えない
    static void AssignNewMembers(CCL::ECS::World &world);

private:
    // 全小隊に共通の、このフレームの入力。Update の先頭で1回だけ集める
    struct SquadFrame
    {
        bool                         ordered        = false; // 小隊突撃が指示されていて、プレイヤーもいる
        bool                         attacking      = false; // 攻撃の合図が立っている
        DirectX::SimpleMath::Vector3 playerPos      = DirectX::SimpleMath::Vector3::Zero; // プレイヤーの位置（ワールド）
        float                        playerRunSpeed = 0.0f; // 追跡の速さの基準 [m/s]
        float                        dt             = 0.0f; // 敵の時間で進めた経過時間 [秒]
    };

    // ============================================================
    // 入力の収集
    // ============================================================

    // 時間・ボスの命令・プレイヤーを読み、このフレームの入力にまとめる
    // rawDt に敵の時間の倍率を掛けて frame.dt にする。プレイヤーがいなければ ordered は false
    SquadFrame ReadFrame(float rawDt) const;

    // 各小隊の生存数と、各席の位置・速度を集め直す
    // Movement（L03）より前に走るので、集まるのは前フレームの位置
    void GatherMembers(SquadRegistry &reg);

    // ============================================================
    // アンカーの動き
    // ============================================================

    // 小隊1つの段階を決めてアンカーを動かす。squadId で追跡の速さと狙いをずらす
    void UpdateSquad(SquadState &squad, uint8_t squadId, const SquadFrame &frame, const SquadTuning &tuning) const;

    // 次の段階を決める。優先度順に上から調べ、最初に成立したものを採る
    SquadPhase ChoosePhase(const SquadFrame &frame) const;

    // 指示がない間（Idle）、アンカーを先頭の機に重ねておき、その機の速度を返す
    // 降下や追跡が始まった瞬間に、アンカーが原点からでなく機体の今いる場所から動き出す
    // アンカーの速度は 0 にする。先頭の機の速度を渡すと、席0の機が止まらずに流れ続ける
    DirectX::SimpleMath::Vector3 FollowLeadMember(SquadState &squad) const;

    // 降下。入った瞬間の位置から、真下の地面近くまで descendTime 秒かけて降りる
    // この動き自体が予告で、降り切った場所から追跡が始まる
    void UpdateDescend(SquadState &squad, SquadPhase prevPhase, const SquadFrame &frame,
                       const SquadTuning &tuning) const;

    // 追跡。アンカーがプレイヤーを追い続ける。速さは曲がるときだけ落とす
    // squadId で、速さのばらつきと、狙いを左右どちらへずらすかを決める
    void UpdateChase(SquadState &squad, uint8_t squadId, SquadPhase prevPhase, const SquadFrame &frame,
                     const SquadTuning &tuning) const;

    // アンカーの向きを、旋回上限の範囲でプレイヤーへ曲げる。aimSide は狙いをずらす左右（-1 / +1）
    // 戻り値は、曲げる前の向きと狙う向きのなす角の cos。曲がるときの減速に使う
    // 終末ロックは持たない。近づくほど見る角度が速く変わり、旋回上限を超えたところで曲がりきれなくなる
    float SteerTowardPlayer(SquadState &squad, const DirectX::SimpleMath::Vector3 &playerPos, float aimSide,
                            const SquadTuning &tuning, float dt) const;

    // アンカーの向きを face の方へ少しずつ寄せる。face がほぼ 0 なら変えない
    void TurnAnchorToward(SquadState &squad, DirectX::SimpleMath::Vector3 face, float dt) const;

    // アンカーが床へ沈まないように、位置と速度・向きの下向き成分を止める
    // 降下と追跡の最後に必ず通す
    void ClampAltitude(SquadState &squad) const;

    // 小隊番号から -1〜1 の値を作る。追跡の速さの個体差に使う
    // 乱数にしないので、当たり外れを同じ条件で再現できる。黄金比の小数部で、連番でも偏らない
    float SquadVariation(uint8_t squadId) const;

    // ============================================================
    // デバッグ UI
    // ============================================================

    // 小隊の調整値をスライダーで出す
    void DrawTuning(SquadTuning &tuning) const;

    // 生きている小隊のアンカーの位置と段階を、先頭から数個まで出す
    void DrawSquadList(const SquadRegistry &reg) const;

    const char *SquadPhaseName(SquadPhase phase) const;
};
