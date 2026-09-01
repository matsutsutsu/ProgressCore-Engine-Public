// 小隊（5機編隊）の「頭」を動かすシステム。小隊単位の判断はここに集約する。
//
// 層の位置づけ:
//   DroneCommandSystem   … ボスの命令を配る（伝令）
//   SquadUpdateSystem    … 小隊の頭をどこへ動かすか決める（小隊指揮）  ← このファイル
//   DroneFormationSystem … 頭からのオフセットで各機の持ち場を出す（振付師）
//   DroneBrainSystem     … 個体が今どうすべきか決める（個体判断）
//   DroneMovementSystem  … 実際に動かす（実行）
//
// なぜ独立したシステムか:
//   小隊は最大32個しかないのに対し、ドローンは数百〜1000機ある。
//   「小隊の判断」を機体ごとのループの中でやると、同じ計算を5回ずつ繰り返した上に
//   同じ小隊へ並列に書き込むことになる。数の違う仕事は分けて、
//   小隊の計算は小隊の数だけ回す。
//
// このシステムは単スレッド。SquadState への書き込みはここだけに閉じる。
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"
#include "Game/Logics/AI/Drone/Squad/SquadState.h"
#include "Engine/Transform/Components/TransformComponent.h"

class SquadUpdateSystem : public CCL::ECS::IfSystem<
                              SquadUpdateSystem,
                              CCL::ECS::Read<DroneComponent>,
                              CCL::ECS::Read<TransformComponent>>
{
  public:
    SquadUpdateSystem()
        : IfSystem("SquadUpdateSystem")
    {
    }
    ~SquadUpdateSystem() override = default;

    void Initialize() override;

    // 伝令が配った陣形指示を読んでから小隊の行動を決める。
    std::vector<const char *> GetRunAfter() const override { return {"DroneCommandSystem"}; }

    void Update(float dt) override;

  private:
    // SquadRegistry が未登録なら作る。
    //
    // シーンが World を作り直しても復帰できるよう、Update からも毎フレーム呼ぶ。
    // データを持つ側のシステムが自分で用意する形にして、シーン側に依存させない。
    void EnsureRegistry();

    // 小隊1個ぶんのフェーズ遷移と頭の移動。
    void UpdateSquad(SquadState                         &squad,
                     uint8_t                             squadId,
                     bool                                ordered,
                     bool                                attacking,
                     const DirectX::SimpleMath::Vector3 &bossPos,
                     const DirectX::SimpleMath::Vector3 &playerPos,
                     const SquadTuning                  &tuning,
                     float                               dt);

    // 頭をプレイヤーへ向けて曲げる（旋回上限つき）。
    //
    // 無制限に追尾させないのが要点。避けても曲がって当たる攻撃になると
    // ジャスト回避というゲームの中心が成立しなくなる。
    // 一定距離まで詰めたら曲がるのをやめ、そこから先は直線＝回避できる窓にする。
    void SteerTowardPlayer(SquadState                         &squad,
                           const DirectX::SimpleMath::Vector3 &playerPos,
                           const SquadTuning                  &tuning,
                           float                               dt) const;

    // 頭が床へ沈まないように高度を止める。全フェーズの移動計算の最後に必ず通す。
    //
    // 位置だけ止めても直らない。速度と向きの下向き成分も一緒に抜くこと。
    // 追従機は「1つ前の機体の速度」を先渡しで受け取るので、頭の速度が
    // 下を向いたままだと胴体が床へ押し込まれ続ける。
    void ClampAltitude(SquadState &squad, const SquadTuning &tuning) const;

    // ---- 調整値（グレーボックスで実測して詰める）----

    // 待機時、ボス頭上のどの高さに小隊を置くか。地上の間合いを塞がない高さ。
    static constexpr float kRegroupHeight = 22.0f;

    // 待機時、ボス中心からの水平距離。小隊ごとに角度をずらして並ぶ。
    static constexpr float kRegroupRadius = 18.0f;



    // 接近位置がゆっくり回る速さ（rad/秒）。止まって見えないようにする。
    static constexpr float kApproachDrift = 0.35f;






    // 待機・接近時に頭を目標へ寄せるバネの強さ。
    static constexpr float kAnchorGain = 1.8f;

    // 上のバネの減衰（追従の滑らかさ）。
    static constexpr float kAnchorDamping = 3.0f;

    // 待機・接近時の最高速度（m/秒）。突入より遅くして緩急を作る。
    static constexpr float kCruiseSpeed = 26.0f;

    // 経過時間。待機・接近位置をゆっくり回すのに使う。
    float m_elapsed = 0.0f;
};
