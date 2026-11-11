#pragma once
// シーンのロードを跨いで残す Entity に付けるタグ。SceneManager::LoadScene の全消去から外れる
// World::Clear（アプリの終了など）はこのタグを見ずにすべて破棄する

namespace Tag
{
    // メンバを持たないタグ。付いているかどうかだけが意味を持つ
    struct PersistentTag
    {
    };
} // namespace Tag