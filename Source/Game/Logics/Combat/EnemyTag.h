#pragma once

namespace Tag {

    // 【タグ】このコンポーネントを持つEntityは「敵」とみなす。
    //
    // なぜ要るか:
    //   敵かどうかの判定が、場所ごとにバラバラな暗黙ルールになっていた。
    //     DefeatSlowMotionSystem … 「PlayerComponent を持たない HealthComponent」＝敵
    //     ボス判定               … BossActionComponent の有無
    //   ボス1体＋ドローンなら成立するが、雑魚を足した瞬間に破綻する。
    //   「HPを持つ非プレイヤー」には壊せる置物や味方NPCも入りうるし、
    //   ボス以外の敵を数えたいときに書く条件が場所ごとに増えていく。
    //
    //   陣営(TeamID)を復活させない理由:
    //     命中の敵味方判定は物理レイヤー(PLAYER_ATTACK/BOSS_ATTACK)が既に持っている。
    //     ここで欲しいのは「敵の一覧が引けること」だけなので、空タグ1個で足りる。
    //
    // 使いどころ:
    //   撃破カウント、ロックオン候補、ウェーブのクリア判定など「敵を数える/探す」処理。
    struct EnemyTag {};

}
