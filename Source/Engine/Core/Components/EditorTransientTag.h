#pragma once
// エディタが一時的に置いた Entity に付けるタグ。シーンを保存するとき、書き出しから外す

namespace Tag
{
    // メンバを持たないタグ。付いているかどうかだけが意味を持つ
    struct EditorTransientTag
    {
    };
} // namespace Tag
