#pragma once
// ===========================================================================
// GameRng.h
// ---------
// 役割:
//   ゲーム実行中の各種確率判定(クリティカル・ドロップ抽選・効果のばらつき等)に
//   使う擬似乱数生成器。World Resource として1つ常駐させ、各Systemが共有して引く。
//
// 責務:
//   - 決定的(シード再現可能)な乱数列の生成と、確率/範囲ヘルパの提供のみ。
//     「その乱数を何の確率に使うか」の意味づけは呼び出し側が持つ。
//
// アーキテクチャ上の位置付け:
//   _world->AddResource<GameRng>(GameRng{seed}) で初期化時に登録し、
//   _world->GetResource<GameRng>() で取得して使う。
//
// 設計方針:
//   - std::rand は使わない(グローバル状態・品質と実装が処理系依存)。代わりに
//     標準の std::mt19937(メルセンヌ・ツイスタ)を用いる。実績があり高品質で、
//     同一シードなら全プラットフォームで同一の乱数列が得られる(再現性の担保)。
//   - 64bit版(mt19937_64)ではなく32bit版で十分。状態は本クラスが内部に閉じ込め、
//     スレッド共有はしない(単一スレッドのSystemから使う前提)。
// ===========================================================================
#include <random>
#include <cstdint>

/// @brief ゲーム用の決定的擬似乱数生成器(std::mt19937ラッパ)。
/// @note  World Resource として常駐させ、各Systemから共有して使用する。
class GameRng
{
public:
    /// @brief 既定シードで初期化する。
    GameRng() : _engine(0xC0FFEEu) {}

    /// @brief 指定シードで初期化する。
    /// @param seed 乱数列を決定する種。同じ種なら同じ列を再現する。
    explicit GameRng(uint32_t seed) : _engine(seed) {}

    /// @brief シードを再設定する(エンカウント開始時などにやり直したい場合に使う)。
    void Seed(uint32_t seed) { _engine.seed(seed); }

    /// @brief [0.0, 1.0) の一様乱数を返す。確率判定の基本。
    float NextFloat()
    {
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        return dist(_engine);
    }

    /// @brief 指定確率で true を返す。
    /// @param probability 成功確率[0,1]。0以下は常にfalse、1以上は常にtrue。
    bool Chance(float probability)
    {
        if (probability <= 0.0f) return false;
        if (probability >= 1.0f) return true;
        return NextFloat() < probability;
    }

    /// @brief [minV, maxV) の一様乱数(float)を返す。
    float RangeFloat(float minV, float maxV)
    {
        std::uniform_real_distribution<float> dist(minV, maxV);
        return dist(_engine);
    }

    /// @brief [minV, maxV] の一様乱数(int、両端を含む)を返す。
    int RangeInt(int minV, int maxV)
    {
        std::uniform_int_distribution<int> dist(minV, maxV);
        return dist(_engine);
    }

private:
    std::mt19937 _engine; ///< メルセンヌ・ツイスタ本体(32bit)
};