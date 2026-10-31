#pragma once
// ===========================================================================
// 概要: 小隊（5機編隊）のアンカーを動かす System。小隊単位の判断はここに集める。
//
// 責務:
//   - 各小隊の生存数と、各席の前フレームの位置・速度を集める
//   - 段階（Idle / Descend / Chase）を決め、アンカーを動かす（待機は BT が他の陣形で作る）
//   - 生成された機を小隊の空き席へ入れる（AssignNewMembers。DroneManagerSystem が生成の後に呼ぶ）
//   - 小隊の調整値と一覧のデバッグ UI
//
// 備考:
//   各機の持ち場はアンカーを起点に陣形（Formations_Squad.h）が出す。ここはアンカーだけを動かす。
//
//   機体ごとのループで行うと、同じ小隊の計算を5回繰り返し、同じ小隊へ並列に書き込むことになる。
//   そこで小隊単位の独立した System にし、単スレッドで回す。
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
    using Base = CCL::ECS::IfSystem<SquadUpdateSystem,
                                    CCL::ECS::Read<DroneComponent>,
                                    CCL::ECS::Read<TransformComponent>>;

public:
    SquadUpdateSystem() : IfSystem("SquadUpdateSystem") {}

    void Initialize() override;

    /**
     * @brief 同じステージで先に走らせる System を宣言する。
     *
     * 命令と、生成時に振った小隊の席を読んでから小隊の行動を決める。
     * BehaviorTreeSystem は間接的に後ろになるが、名指ししないと起動時に「順序不定」と報告される。
     */
    std::vector<const char *> GetRunAfter() const override
    {
        return {"BehaviorTreeSystem", "DroneCommandSystem", "DroneManagerSystem"};
    }

    /**
     * @brief テンプレート引数以外に読む型（ボスの命令）を申告する。
     *
     * テンプレート引数に足すと ForEach の絞り込み条件にもなってしまう。
     */
    std::vector<CCL::ECS::TypeID> GetReadTypes() const override
    {
        auto types = Base::GetReadTypes();
        types.push_back(CCL::ECS::TypeInfo<BossCommandComponent>::ID());
        return types;
    }

    void Update(float dt) override;

    void OnGui() override; // 小隊の調整値と、生きている小隊の一覧

    /**
     * @brief まだ小隊に入っていない機を、空いている席へ入れる。
     *
     * 所属は一度決めたら維持する。振り直すと生き残った機の所属まで入れ替わり、同じ部隊に見えない。
     *
     * @param world ドローンが居る World。
     * @note DroneManagerSystem が生成の直後に呼ぶ（書き込みは呼んだ側の System の中で行われる）。
     */
    static void AssignNewMembers(CCL::ECS::World &world);

private:
    // 全小隊に共通の、このフレームの入力。Update の先頭で1回だけ集める
    struct SquadFrame
    {
        bool                         ordered        = false; // 小隊突撃が指示されていて、プレイヤーも居る
        bool                         attacking      = false; // 攻撃の合図が立っている
        DirectX::SimpleMath::Vector3 playerPos     = DirectX::SimpleMath::Vector3::Zero; // プレイヤーの位置（ワールド）
        float                        playerRunSpeed = 0.0f; // 追跡の速さの基準 [m/s]
        float                        dt             = 0.0f; // 敵の時間で進めた経過時間 [秒]
    };

    // ============================================================
    // 入力の収集
    // ============================================================

    /**
     * @brief 時間・ボスの命令・プレイヤーを読み、このフレームの入力にまとめる。
     * @param rawDt スローの掛かっていない経過時間 [秒]。敵の時間の倍率を掛けて frame.dt にする。
     * @return このフレームの入力。プレイヤーが居なければ ordered は false。
     */
    SquadFrame ReadFrame(float rawDt) const;

    /**
     * @brief 各小隊の生存数と、各席の機体の位置・速度を毎フレーム集め直す。
     * @param reg 書き込む全小隊の状態。
     * @note Movement（L03）より前に走るので、集まるのは前フレームの位置になる。
     */
    void GatherMembers(SquadRegistry &reg);

    // ============================================================
    // アンカーの動き
    // ============================================================

    /**
     * @brief 小隊1個ぶんの段階を決め、アンカーを動かす。
     * @param squad   更新する小隊。
     * @param squadId 小隊番号。追跡の個体差（速さ・狙いのずれ）を決める。
     * @param frame   このフレームの入力。
     * @param tuning  小隊の調整値。
     */
    void UpdateSquad(SquadState &squad, uint8_t squadId, const SquadFrame &frame, const SquadTuning &tuning) const;

    /**
     * @brief 次の段階を決める。優先度順に上から判定し、最初に成立したものを採る。
     * @param frame このフレームの入力。
     * @return 次の段階。
     */
    SquadPhase ChoosePhase(const SquadFrame &frame) const;

    /**
     * @brief 指示が無い間（Idle）、アンカーを先頭の機（生きている一番前の席）に重ねておく。
     *
     * 降下や追跡が始まった瞬間に、アンカーが機体の今いる場所から動き出すようにするため（原点から飛んでこない）。
     * 速度は0にする。先頭の機の速度を渡すと、席0の機が自分の速度を先渡しで受け取り、止まらずに流れ続ける。
     *
     * @param squad アンカーを置く小隊。
     * @return 先頭の機の速度。生きている機が居なければ0。
     */
    DirectX::SimpleMath::Vector3 FollowLeadMember(SquadState &squad) const;

    /**
     * @brief 降下（Descend）: 降下に入った瞬間の位置から、真下の地面近くまで descendTime 秒かけて降りる。
     *
     * この動きそのものが予告で、降り切った場所から追跡が始まる。
     * 頭上円陣の輪の上から始まるので、小隊はボスの周りから降りてくる。
     *
     * @param squad     動かす小隊。
     * @param prevPhase 前フレームの段階。降下に入った最初のフレームを見分ける。
     * @param frame     このフレームの入力。
     * @param tuning    小隊の調整値（降り切る高さと時間）。
     */
    void UpdateDescend(SquadState &squad, SquadPhase prevPhase, const SquadFrame &frame,
                       const SquadTuning &tuning) const;

    /**
     * @brief 追跡（Chase）: アンカーがプレイヤーを追い続ける。速さは曲がるときだけ落とす。
     * @param squad     動かす小隊。
     * @param squadId   小隊番号。速さのばらつきと、狙いを左右どちらへずらすかを決める。
     * @param prevPhase 前フレームの段階。追跡に入った最初のフレームの速さを決める。
     * @param frame     このフレームの入力。
     * @param tuning    小隊の調整値。
     */
    void UpdateChase(SquadState &squad, uint8_t squadId, SquadPhase prevPhase, const SquadFrame &frame,
                     const SquadTuning &tuning) const;

    /**
     * @brief アンカーの向きをプレイヤーへ、旋回上限の範囲で曲げる。
     *
     * 終末ロック（一定距離から直線に固定する処理）は持たない。近づくほどプレイヤーを見る角度が速く変わり、
     * 旋回上限を超えた時点で自然に曲がりきれなくなる。
     *
     * @param squad     向きを曲げる小隊。
     * @param playerPos プレイヤーの位置（ワールド）。
     * @param aimSide   狙う点をプレイヤーの左右どちらへずらすか（-1 / +1）。
     * @param tuning    小隊の調整値（旋回上限・ずれの量など）。
     * @param dt        経過時間 [秒]。
     * @return 曲げる前の向きと狙う方向のなす角の cos（1=正面、0=真横）。曲がるときの減速に使う。
     */
    float SteerTowardPlayer(SquadState &squad, const DirectX::SimpleMath::Vector3 &playerPos, float aimSide,
                            const SquadTuning &tuning, float dt) const;

    /**
     * @brief アンカーの向きを face の方へ少しずつ寄せる。降下が使う。
     * @param squad 向きを変える小隊。
     * @param face  向けたい方向。長さはどうでもよい（ほぼ0なら向きを変えない）。
     * @param dt    経過時間 [秒]。
     */
    void TurnAnchorToward(SquadState &squad, DirectX::SimpleMath::Vector3 face, float dt) const;

    /**
     * @brief アンカーが床へ沈まないように、位置と速度・向きの下向き成分を止める。降下と追跡の最後に必ず通す。
     * @param squad 高度を止める小隊。
     */
    void ClampAltitude(SquadState &squad) const;

    /**
     * @brief 小隊番号から -1〜1 の値を作る。追跡の個体差（速さ）に使う。
     *
     * 乱数にしないので、当たった・外れたを同じ条件で再現して調整できる。
     * 黄金比の小数部を使うので、番号が連続していても値が偏らない。
     *
     * @param squadId 小隊番号。
     * @return -1〜1 の値。同じ番号からは常に同じ値が返る。
     */
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
