#pragma once
// ===========================================================================
// 概要: 小隊（5機編隊）のアンカーを動かす System。小隊単位の判断はここに集める。
//
// 責務:
//   - 各小隊の生存数と、各席の前フレームの位置・速度を集める
//   - 段階（Regroup / Gather / Chase）を決め、アンカーを動かす
//   - 生成された機を小隊の空き席へ入れる（AssignNewMembers。DroneManagerSystem が生成の後に呼ぶ）
//   - 小隊の調整値と一覧のデバッグ UI
//
// 備考:
//   ドローン系 System の分担は次のとおり。
//     DroneCommandSystem   … ボスの命令を各機へ配る
//     SquadUpdateSystem    … 小隊のアンカーをどこへ動かすか決める（この System）
//     DroneFormationSystem … アンカーを起点に各機の持ち場を出す（Formations_Squad.h）
//     DroneBrainSystem     … 個体が今どうすべきか決める
//     DroneMovementSystem  … 実際に動かす
//
//   独立した System にしているのは、小隊が最大32個なのに対しドローンは1000機あるため。
//   機体ごとのループで行うと同じ計算を5回繰り返し、同じ小隊へ並列に書き込むことになる。
//
//   単スレッドで回し、SquadState への書き込みはここだけに限る。
//   実行中に触らない調整値は .cpp にまとめてある。
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
     * DroneCommandSystem が配った陣形指示を読んでから小隊の行動を決める。
     *
     * BehaviorTreeSystem も直接名指しする。間接的に後ろになるだけでは、起動時に「順序不定」と報告される。
     *
     * DroneManagerSystem は初回配備で squadId / slotInSquad を書くので、その後に走る。
     */
    std::vector<const char *> GetRunAfter() const override
    {
        return {"BehaviorTreeSystem", "DroneCommandSystem", "DroneManagerSystem"};
    }

    /**
     * @brief テンプレート引数以外に読む型を申告する。
     *
     * Update がボスの BossCommandComponent を View で引く。
     * テンプレート引数には足せない（ForEach の絞り込み条件を兼ねているため）。
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
     * DroneManagerSystem が生成の直後に呼ぶ（書き込みは呼んだ側の System の中で行われる）。
     * 撃破の後には呼ばれないので、撃破で空いた席は埋め直さない（隊列は欠員を飛ばして詰める）。
     *
     * 所属は一度決めたら維持する。
     * 振り直すと生き残った機の所属まで入れ替わり、同じ部隊で組んでいるように見えない。
     *
     * @param world ドローンが居る World。
     */
    static void AssignNewMembers(CCL::ECS::World &world);

private:
    // 全小隊に共通の、このフレームの入力。Update の先頭で1回だけ集める。
    struct SquadFrame
    {
        bool                         ordered        = false; // 小隊突撃が指示されていて、プレイヤーも居る
        bool                         attacking      = false; // 攻撃の合図が立っている
        DirectX::SimpleMath::Vector3 bossPos        = DirectX::SimpleMath::Vector3::Zero;
        DirectX::SimpleMath::Vector3 playerPos      = DirectX::SimpleMath::Vector3::Zero;
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
     * @brief 各小隊の生存数と、各席の機体の位置・速度を集め直す。
     *
     * 生存数は毎フレーム数え直す。撃破で小隊が痩せるため。
     * 各席の位置と速度は、追従機が「自分の前の機体」を追うのに要る（Formations_Squad.h）。
     *
     * @param reg 書き込む全小隊の状態。
     * @note Movement（L03）より前に走るので、集まるのは前フレームの位置になる。
     */
    void GatherMembers(SquadRegistry &reg);

    // ============================================================
    // 段階ごとの動き
    // ============================================================

    /**
     * @brief 小隊1個ぶんの段階を決め、アンカーを動かす。
     * @param squad       更新する小隊。
     * @param squadId     小隊番号。追跡の個体差（速さ・狙いのずれ）を決める。撃破で変わらないので、追跡の途中で値が跳ねない。
     * @param rank        生きている小隊の中での順番（0〜activeCount-1）。待機・集合の輪の上の位置を決める。
     * @param activeCount 生きている小隊の数。
     * @param frame       このフレームの入力。
     * @param tuning      小隊の調整値。
     */
    void UpdateSquad(SquadState &squad, uint8_t squadId, uint8_t rank, uint8_t activeCount,
                     const SquadFrame &frame, const SquadTuning &tuning) const;

    /**
     * @brief 次の段階を決める。優先度順に上から判定し、最初に成立したものを採る。
     * @param frame このフレームの入力。
     * @return 次の段階。
     */
    SquadPhase ChoosePhase(const SquadFrame &frame) const;

    /**
     * @brief 待機（Regroup）: ボスの頭上の輪を、小隊ごとに角度をずらしてゆっくり回る。
     * @param squad       動かす小隊。
     * @param rank        生きている小隊の中での順番（0〜activeCount-1）。輪の上の位置を決める。
     * @param activeCount 生きている小隊の数。
     * @param frame       このフレームの入力。
     */
    void UpdateRegroup(SquadState &squad, uint8_t rank, uint8_t activeCount, const SquadFrame &frame) const;

    /**
     * @brief 集合（Gather）: ボスの頭上の輪の自分の場所へ集まり、着いたら下へ降りてくる。
     *
     * 小隊ごとに輪の上の角度をずらすので、ボスを囲むように並ぶ。
     * 集まって降りてくる動きそのものが予告で、降り切った場所から追跡が始まる。
     *
     * @param squad       動かす小隊。
     * @param prevPhase   前フレームの段階。集合に入った最初のフレームを見分ける。
     * @param rank        生きている小隊の中での順番（0〜activeCount-1）。輪の上の位置を決める。
     * @param activeCount 生きている小隊の数。
     * @param frame       このフレームの入力。
     * @param tuning      小隊の調整値。
     */
    void UpdateGather(SquadState &squad, SquadPhase prevPhase, uint8_t rank, uint8_t activeCount,
                      const SquadFrame &frame, const SquadTuning &tuning) const;

    /**
     * @brief 追跡（Chase）: アンカーがプレイヤーを追い続ける。後続機はそれに付いていく。
     *
     * 曲がる速さに上限があり、避けられるかどうかはこの上限で決まる。速さは曲がるときだけ落とす。
     * 速さと狙う点は小隊ごとにずらし、全小隊が同じ線に重ならないようにする。
     *
     * @param squad     動かす小隊。
     * @param squadId   小隊番号。速さのばらつきと、狙いを左右どちらへずらすかを決める。
     * @param prevPhase 前フレームの段階。追跡に入った最初のフレームを見分ける。
     * @param frame     このフレームの入力。
     * @param tuning    小隊の調整値。
     */
    void UpdateChase(SquadState &squad, uint8_t squadId, SquadPhase prevPhase, const SquadFrame &frame,
                     const SquadTuning &tuning) const;

    /**
     * @brief アンカーの向きをプレイヤーへ、旋回上限の範囲で曲げる。
     *
     * 狙う点はプレイヤーの左右へずらし、近づくほどずれを縮めて最後はプレイヤー本人を狙う。
     *
     * 終末ロック（一定距離から直線に固定する処理）は持たない。
     * 近づくほどプレイヤーを見る角度は速く変わり、旋回上限を超えた時点で曲がりきれなくなる。
     * 至近距離で直線になるのは、この上限から自然に起きる。
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
     * @brief アンカーを目標点へバネで寄せる。待機と集合が使う。
     *
     * 瞬間移動させず、上限の速さで飛んでいく。
     * ただしアンカーの位置を一度も決めていなければ、目標へ瞬間移動させる（ワールド原点から飛んでこないように）。
     *
     * @param squad   動かす小隊。
     * @param desired 目標点（ワールド）。
     * @param dt      経過時間 [秒]。
     */
    void SpringAnchorTo(SquadState &squad, const DirectX::SimpleMath::Vector3 &desired, float dt) const;

    /**
     * @brief アンカーの向きを face の方へ少しずつ寄せる。待機と集合が使う。
     * @param squad 向きを変える小隊。
     * @param face  向けたい方向。長さはどうでもよい（ほぼ0なら向きを変えない）。
     * @param dt    経過時間 [秒]。
     */
    void TurnAnchorToward(SquadState &squad, DirectX::SimpleMath::Vector3 face, float dt) const;

    /**
     * @brief アンカーが床へ沈まないように高度を止める。全段階の最後に必ず通す。
     *
     * 位置だけでなく、速度と向きの下向き成分も抜く。
     * 追従機は「1つ前の機体の速度」を先渡しで受け取るので、アンカーが下を向いたままだと後続機が床へ押し込まれ続ける。
     *
     * @param squad 高度を止める小隊。
     */
    void ClampAltitude(SquadState &squad) const;

    /**
     * @brief 小隊番号から -1〜1 の値を作る。追跡の個体差（速さ・狙いのずれ）に使う。
     *
     * 乱数にしないのは、当たった・外れたの原因を同じ条件で再現して調整できるようにするため。
     * 黄金比の小数部を使うと、番号が連続していても値が偏らずに散らばる。
     *
     * @param squadId 小隊番号。
     * @return -1〜1 の値。同じ番号からは常に同じ値が返る。
     */
    float SquadVariation(uint8_t squadId) const;

    // ============================================================
    // デバッグ UI
    // ============================================================

    /**
     * @brief 小隊の調整値をスライダーで出す。
     *
     * しなり具合と避けやすさは目で見ないと決まらないので、実行中に変えられるようにする。
     *
     * @param tuning 書き換える調整値。
     */
    void DrawTuning(SquadTuning &tuning) const;

    /**
     * @brief 生きている小隊のアンカーの位置と段階を、先頭から6個まで出す。
     * @param reg 表示する全小隊の状態。
     */
    void DrawSquadList(const SquadRegistry &reg) const;

    /**
     * @brief 一覧に出す段階の名前を返す。
     * @param phase 名前にする段階。
     * @return 段階の名前（静的な文字列）。
     */
    const char *SquadPhaseName(SquadPhase phase) const;

    float m_elapsed = 0.0f; // 待機の輪をゆっくり回すのに使う経過時間 [秒]（敵の時間）
};
