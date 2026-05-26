/**
 * @file   CCL_System.h
 * @brief  System関連のヘッダを一括でinclude するための便利ヘッダ (傘ヘッダ)
 *
 * @details
 *  System「実装ファイル」(○○System.cpp) で1行 include するだけで、
 *  SystemBase, IfSystem, Read<T>/Write<T> を全て使えるようにします。
 *
 *  ヘッダー側で「Systemを宣言するだけ」のケースでは、より軽量な
 *  CCL_SystemBase.h を直接 include することを推奨します。
 *
 *  @par 推奨される使い分け
 *   - MovementSystem.h  : `#include "CCL_SystemBase.h"` (軽い)
 *   - MovementSystem.cpp: `#include "CCL_System.h"` (フル装備)
 *
 * @author CCL ECS Library
 */
#pragma once

#include "CCL_SystemBase.h"
#include "CCL_SystemAccess.h"
#include "CCL_IfSystem.h"
