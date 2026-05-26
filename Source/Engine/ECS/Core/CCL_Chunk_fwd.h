/**
 * @file   CCL_Chunk_fwd.h
 * @brief  Chunkクラスの前方宣言ヘッダ
 *
 * @details
 *  Chunk*をメンバとして保持するクラスのヘッダから、
 *  CCL_Chunk.h を直接includeせずに済ませるためのヘッダです。
 *  SystemBase.hなどから利用することでビルド速度を改善します。
 *
 *  Chunkの実体メソッドを呼び出す側 (.cpp等) は CCL_Chunk.h を直接 includeしてください。
 *
 * @author CCL ECS Library
 */
#pragma once

namespace CCL::ECS::Core
{
    /// @brief Chunkクラス(同一アーキタイプのEntityをまとめたSoAデータストア)
    class Chunk;
}
