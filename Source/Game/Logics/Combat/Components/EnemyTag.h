#pragma once

namespace Tag
{
    // 敵であることを表すタグ。倒した数・ロックオンの候補・全滅の判定が使う
    // HealthComponent の有無で数えると、HP を持つ置物や味方まで敵に入るので、このタグだけで決める
    struct EnemyTag
    {
    };

}
