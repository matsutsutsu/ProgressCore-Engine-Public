#pragma once
#include <type_traits> // for std::underlying_type

// enum class にすることで型安全にする（uint32_tを指定）
enum class Layer : uint32_t
{
    None    = 0,
    Default = 1 << 0,
    Player  = 1 << 1,
    Enemy   = 1 << 2,
    Bullet  = 1 << 3,
    Wall    = 1 << 4,
    Item    = 1 << 5,
    EnemyBullet = 1 << 6, // ★追加
    // 便利セット
    All = 0xFFFFFFFF
};

// ビット演算を楽にするための演算子オーバーロード
inline Layer operator|(Layer a, Layer b)
{
    return static_cast<Layer>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline Layer operator&(Layer a, Layer b)
{
    return static_cast<Layer>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

// 判定用（ANDの結果が0じゃないかチェック）
inline bool CheckHit(Layer mask, Layer target)
{
    return (static_cast<uint32_t>(mask & target) != 0);
}