#pragma once
// ===========================================================================
// CCL_Common.h
// ------------
// 【集約ヘッダ / Umbrella Header】
//
// このファイル自体には実装を持たず、ECS Common レイヤーを構成する
// 5つの個別ヘッダをまとめて include するだけのファイルです。
//
// これにより:
//   - 既存コードは `#include "CCL_Common.h"` のままで何も変更不要
//   - 新規コードは必要なヘッダだけを個別 include することでビルド速度UP
//
// という双方の利点を両立できます。
//
// ---------------------------------------------------------------------------
// 構成（責務分割）
// ---------------------------------------------------------------------------
//   CCL_Types.h        : 基本ID型 (EntityID/TypeID/...)、定数、EntityHandle
//   CCL_IDEmitter.h    : ID 採番ユーティリティ
//   CCL_TypeErasure.h  : 型消去ユーティリティ (Destructor / Copier)
//   CCL_TypeInfo.h     : TypeInfo<T> / TypeData（型情報レコード）
//   CCL_Archetype.h    : Archetype / ArchetypeHelper（型構成集合）
//
// ---------------------------------------------------------------------------
// 推奨: 新規ファイルでは必要な個別ヘッダのみを include してください。
//   例) コンポーネント型 ID だけ欲しい場合 → #include "CCL_TypeInfo.h"
// ===========================================================================

#include "CCL_Types.h"
#include "CCL_IDEmitter.h"
#include "CCL_TypeErasure.h"
#include "CCL_TypeInfo.h"
#include "CCL_Archetype.h"
