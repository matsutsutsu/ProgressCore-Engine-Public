#pragma once

// ここが「唯一の定義場所」になります。
// シェーダーを増やしたいときは、ここに行を追加するだけです。
#define SHADER_ID_LIST(X) \
    X(Basic) \
    X(Lambert) \
    X(Phong) \
    X(Toon) \
    X(ShadowMap) \
    X(Outline) \
    X(PBR) \
    /* X(NewShader) ← ここに追加すれば自動で反映される */

// 1. Enumの自動生成
enum class ShaderId
{
#define DEF_ENUM(name) name,
    SHADER_ID_LIST(DEF_ENUM)
#undef DEF_ENUM

    EnumCount
};

// 2. 文字列配列の自動生成
// inline変数 (C++17) を使うことでヘッダーに書いても多重定義エラーになりません
inline const char* const ShaderIdNames[] =
{
    #define DEF_STR(name) #name,
    SHADER_ID_LIST(DEF_STR)
    #undef DEF_STR
};