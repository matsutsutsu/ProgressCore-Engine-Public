#pragma once
// ===========================================================================
// 概要: EntityID / TypeID / SystemID を連番で発行する採番器
//
// 責務:
//   - 各種 ID をプロセス全体で一意に発行する
// ===========================================================================

#include "CCL_Types.h"

namespace CCL::ECS
{
    // Entity やコンポーネントの型の ID を連番で発行する
    // カウンタは inline static のメンバでなく関数の中の static に置き、翻訳単位をまたいでも1つにする
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
