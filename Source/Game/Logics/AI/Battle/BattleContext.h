// 1対1ボス戦の「世界の客観的な事実」を1箇所に集約する共有リソース。
//
// 設計思想 :
//   ゲームAIの「ブラックボード」と、ECSの「リソース」を役割で使い分ける。
//     - BattleContext（このリソース）= 世界に1つの客観的事実
//         誰から見ても同じ値（プレイヤー座標・ドローン残数など）。
//     - AIBlackboard（各AIが持つ）     = そのAIエージェント個人の主観的認識
//         「このボスから見た距離」「このドローンの担当スロット」など。
//
//   各AIは BattleContext（事実）を読み、自分の Blackboard（認識）を更新し、
//   その Blackboard を見て BT が判断する。
//   → 1000機が同じ事実を重複保持せず、個体判断は各自のブラックボードで独立。
//
// 1対1なので「敵を探す」処理は不要。プレイヤーもボスも唯一なので、
// ここに ID と座標を置けば全システムが探索ゼロで直接参照できる。
#pragma once
#include "Engine/ECS/Common/CCL_Types.h"
#include <DirectXMath.h>

// 戦場全体で共有される唯一の「戦況」
struct BattleContext
{
    // プレイヤーとボスのEntityID
    // 今は２つだけど、今後雑魚敵など増やす予定
    CCL::ECS::EntityID playerEntity = CCL::ECS::InvalidEntityID;
    CCL::ECS::EntityID bossEntity   = CCL::ECS::InvalidEntityID;

    // プレイヤーとボスの座標。
    //
    // 【これは足元】
    //   Transform の原点はキャラの足元にある。狙う／当てるのにそのまま使うと
    //   身長ぶんまるごとずれる。プレイヤーのカプセルは
    //   halfHeight 3.5 + radius 1.0 ＝ 全高 9m あるので、足元を基準にすると
    //   胸を貫いても「4.5m 離れている」と判定される。
    //   狙いと当たり判定には下の playerCenter を使うこと。
    DirectX::XMFLOAT3 playerPos = {0, 0, 0};
    DirectX::XMFLOAT3 bossPos   = {0, 0, 0};

    // プレイヤーの体の中心。書き手は BattleContextSyncSystem。
    //
    //   足元 ＋ (halfHeight + radius)。この式は JoltCharacterSetupSystem が
    //   カプセルを載せるときに使っている「足元→中心オフセット」と同じもの。
    //   別々に決めると、判定の基準と実際の体の位置がずれる。
    //
    //   狙う・当てるのはすべてこちら。移動や設置の基準は playerPos のまま。
    DirectX::XMFLOAT3 playerCenter = {0, 0, 0};

    // プレイヤーの速度 [m/s]。書き手は BattleContextSyncSystem。
    //
    // 何に使うか:
    //   「着弾までにプレイヤーがどこへ動くか」を読む＝偏差射撃。
    //   弾幕のように飛翔時間のある攻撃は、今いる場所を撃つと必ず後ろへ外れる。
    //
    // なぜここに置くか:
    //   誰から見ても同じ客観的な事実なので BattleContext の担当。
    //   各AIが個別に前フレーム座標を覚えて差分を取ると、覚える場所が
    //   AIの数だけ増えるうえ、平滑化の強さも別々になって挙動が揃わない。
    //
    // 座標の差分から出しているので、テレポートやノックバックでは
    // 一瞬だけ大きな値が出る。平滑化と上限で均してある（実装側のコメント参照）。
    DirectX::XMFLOAT3 playerVelocity = {0, 0, 0};

    // ドローンの生存数
    // 残数がそのままボスの体力を表す。DroneManagerSystem が毎フレーム数えて書き込む
    // （誰から見ても同じ値なので、個体の Blackboard ではなくここに置く）
    int droneAliveCount = 1000;

    // プレイヤーが存在するか
    bool HasPlayer() const
    {
        return playerEntity != CCL::ECS::InvalidEntityID;
    }
    bool HasBoss() const
    {
        return bossEntity != CCL::ECS::InvalidEntityID;
    }
};
