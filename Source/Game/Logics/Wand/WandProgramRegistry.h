#pragma once
// ===========================================================================
// WandProgramRegistry.h
// ---------------------
// @file
// @brief コンパイル済み杖プログラムを集中保持する World Resource。
//        弾は ProgramID(=本配列のindex) でここを参照する。
//
// ライフサイクル原則:
//   - 登録/更新はショップ遷移時のみ（戦闘中は不変）。
//   - 戦闘中に配列が再配置されないため、飛行中の弾が持つ ProgramID は常に有効。
//
// @warning Resource アクセスは ECS の依存解析(Read/Write Types)の対象外。
//          書き込み(RegisterOrUpdate)を行うのは WandCompileSystem のみとし、
//          かつ弾発射(読み取り)より前のステージで実行することで競合を避ける。
// ===========================================================================

#include <vector>
#include <cassert>
#include "WandTypes.h"
#include "CompiledWandProgram.h"

/**
 * @brief コンパイル済み杖プログラムの集中管理庫（World Resource として登録する）。
 */
class WandProgramRegistry
{
public:
    /**
     * @brief プログラムを新規登録、または既存スロットを上書き更新する。
     *
     * existing が有効なら同じスロットを再利用する（戦闘外更新のためメモリの無制限増加を防ぐ）。
     * 無効なら末尾に追加し、新しい ProgramID を発番する。
     *
     * @param existing 既存の ProgramID（無ければ InvalidProgramID）
     * @param prog     登録するコンパイル済みプログラム
     * @return 確定した ProgramID
     */
    ProgramID RegisterOrUpdate(ProgramID existing, const CompiledWandProgram& prog)
    {
        if (existing != InvalidProgramID && existing < _programs.size())
        {
            _programs[existing] = prog; // スロット再利用
            return existing;
        }
        _programs.push_back(prog);
        return static_cast<ProgramID>(_programs.size() - 1);
    }

    /**
     * @brief ProgramID からプログラム実体を取得する（O(1)）。
     * @warning 無効 ID は assert で停止。発射時に有効な ProgramID のみを弾へ焼き込む運用を守る。
     */
    const CompiledWandProgram& Get(ProgramID id) const
    {
        assert(id < _programs.size() && "Invalid ProgramID passed to WandProgramRegistry::Get");
        return _programs[id];
    }

    /// @brief ProgramID が有効範囲内かを判定する。
    bool IsValid(ProgramID id) const { return id < _programs.size(); }

    /// @brief 登録済みプログラム数。
    size_t Count() const { return _programs.size(); }

private:
    /// @brief プログラム実体。配列 index がそのまま ProgramID。
    std::vector<CompiledWandProgram> _programs;
};