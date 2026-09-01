#pragma once
// ===========================================================================
// CCL_IDEmitter.h
// ---------------
// EntityID / TypeID / ComponentID / SystemID といった ECS の各種IDを
// プロセス全体で一意に発行するためのジェネレータです。
// ===========================================================================

#include "CCL_Types.h"


namespace CCL::ECS
{
    /**
     * @brief 各種IDの発行（採番）を一手に引き受けるユーティリティクラス。
     *
     * 「社員番号発行機」のようなイメージで、新しいエンティティやコンポーネント
     * 型が登場するたびにユニークなIDをインクリメントしながら返します。
     *
     * @note
     *   メンバとして `inline static` 変数を持たず、各メソッド内に `static`
     *   ローカル変数を置くことで、DLLや翻訳単位を跨いだ「変数の分裂」を
     *   原理的に防止しています。これによりプロセス全体で唯一性が保証されます。
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
