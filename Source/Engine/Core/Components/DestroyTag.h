#pragma once
// 削除待ちの Entity に付けるタグ。World::Destroy が付け、HierarchyCleanUpSystem が子ごと破棄する

namespace Tag
{
    // メンバを持たないタグ。付いているかどうかだけが意味を持つ
    struct DestroyTag
    {
    };

} // namespace Tag