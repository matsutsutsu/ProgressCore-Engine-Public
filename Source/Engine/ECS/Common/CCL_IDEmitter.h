#pragma once
// ===========================================================================
// 概要: EntityID / TypeID / SystemID を連番で発行する採番器。
//
// 責務:
//   - 各種 ID をプロセス全体で一意に発行する
// ===========================================================================

#include "CCL_Types.h"

namespace CCL::ECS
{
    /**
     * @brief 各種 ID の発行（採番）を一手に引き受けるユーティリティクラス。
     *
     * 新しい Entity やコンポーネント型が登場するたびに、連番で一意な ID を返す。
     *
     * @note カウンタをメンバの `inline static` 変数ではなく、各関数内の `static`
     *       ローカル変数に置いている。DLL や翻訳単位をまたいでカウンタが
     *       複数できてしまう「変数の分裂」を防ぎ、プロセス全体で一意にするため。
     */
    class ID_Emitter
    {
    public:
        // 新しい EntityID を採番して返す
        static EntityID EmitEntityID()
        {
            static EntityID s_nextEntityID = 0;
            return s_nextEntityID++;
        }

        // 新しい TypeID を採番して返す
        static TypeID EmitTypeID()
        {
            static TypeID s_nextTypeID = 0;
            return s_nextTypeID++;
        }

        // 新しい SystemID を採番して返す
        static SystemID EmitSystemID()
        {
            static SystemID s_nextSystemID = 0;
            return s_nextSystemID++;
        }
    };

} // namespace CCL::ECS
