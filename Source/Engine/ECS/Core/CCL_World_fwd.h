/**
 * @file   CCL_World_fwd.h
 * @brief  Worldクラスの前方宣言ヘッダ
 *
 * @details
 *  World*をメンバとして保持するクラス(主にSystemBase)のヘッダから、
 *  CCL_World.h を直接includeせずに済ませるためのヘッダです。
 *  Worldの実体メソッドを呼ぶファイルは CCL_World.h を直接includeしてください。
 *
 * @author CCL ECS Library
 */
#pragma once

namespace CCL::ECS::Core
{
    class World; ///< @brief ECSの世界(Chunk群+PendingOps+EventBus)
}
