/// @file  StateTreeAsset.h
/// @brief StateTree の「木の形」を保持する不変アセット。全 Entity で共有される。
///
/// 設計の最重要ポイント 
///   このアセットは「木の構造」と「ノードのパラメータ」だけを持つ。
///   「今どの状態にいるか（インスタンス状態）」は StateTreeComponent 側に置く。
///
/// メモリイメージ:
///   ┌─────────────────────────────────────────────────────┐
///   │ StateTreeAsset  (World Resource。1種類の敵に1個)      │
///   │   states[]     : StateNode の配列（木の骨格）         │
///   │   tasks[]      : NodeRef  の配列（使うタスク種別+位置）│
///   │   conditions[] : NodeRef  の配列（使う条件種別+位置） │
///   │   transitions[]: Transition の配列（遷移ルール）       │
///   │   paramBlob[]  : 全ノードのパラメータを連続配置した塊  │
///   └─────────────────────────────────────────────────────┘
///           ↑ 参照（StateTreeHandle = u32 インデックス）
///   ┌─────────────────────────────────────────────────────┐
///   │ StateTreeComponent  (各 Entity が持つ極小コンポ)      │
///   │   activeStates[]: 今アクティブな状態パス（深さ分）    │
///   └─────────────────────────────────────────────────────┘
///   ※ ノードが個体別の可変状態を必要とする場合は、専用領域を持たず
///     ゲーム側 Component（例: DroneComponent.stateTimer）に持たせる。
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "StateTreeNode.h"   // NodeTypeID, ITask, ICondition の前方参照


namespace CCL::AI
{
    // @brief 敵自身がどのStateTreeのデータを参照するかのハンドル
    // @details 
    struct StateTreeHandle
    {
        uint32_t value = 0xFFFFFFFF;
        bool IsValid() const { return value != 0xFFFFFFFF; }
        bool operator==(const StateTreeHandle& o) const { return value == o.value; }
        bool operator!=(const StateTreeHandle& o) const { return value != o.value; }
    };

    // ====================================================================
    // 基本定数・型
    // ====================================================================

    // アセットのインデックスとして無効値を表す定数。
    constexpr uint16_t ST_INVALID = 0xFFFF;

    // NodeRegistry が発行するノード種別 ID。
    using NodeTypeID = uint32_t;
    constexpr NodeTypeID ST_INVALID_NODE_TYPE = 0xFFFFFFFF;


    // ====================================================================
    // StateNode — 木の1ノード（状態）
    // ====================================================================

    /// @brief StateTree の1状態を表す完全な POD 構造体。
    ///
    /// フラット配列 StateTreeAsset::states に詰められる。
    /// 「子供」「タスク」「条件」「遷移」は全て他の配列への [begin, count) 範囲として持つ。
    /// ポインタを一切使わないことで、配列をそのままシリアライズ可能にする。
    ///
    /// メモリレイアウト (24 byte):
    ///   parent(2) + firstChild(2) + nextSibling(2) + childCount(2)
    ///   + enterCondBegin(2) + enterCondCount(2)
    ///   + taskBegin(2) + taskCount(2)
    ///   + transBegin(2) + transCount(2)
    ///   + pad(4)
    ///
    /// @note states[0] が必ずルートノード。
    struct StateNode
    {
        // --- 親子関係 ---
        uint16_t parent      = ST_INVALID;  ///< 親の states インデックス（ルートは ST_INVALID）
        uint16_t firstChild  = ST_INVALID;  ///< 最初の子の states インデックス
        uint16_t nextSibling = ST_INVALID;  ///< 次の兄弟の states インデックス
        uint16_t childCount  = 0;

        // --- この状態に「入れるか」を判定する条件群 [begin, count) ---
        // 全条件が true のとき初めてこの状態が選択候補になる
        uint16_t enterCondBegin = 0;
        uint16_t enterCondCount = 0;

        // --- 滞在中毎フレーム Tick するタスク群 [begin, count) ---
        uint16_t taskBegin  = 0;
        uint16_t taskCount  = 0;

        // --- 評価する遷移ルール群 [begin, count) ---
        uint16_t transBegin = 0;
        uint16_t transCount = 0;

        // --- デバッグ・エディタ用 ---
        // 実行時には使わない。JSON ロード時に保存、エディタ側で表示。
        uint16_t nameOffset  = 0;   ///< nameBlob 内の名前文字列オフセット
        uint16_t nameLength  = 0;
    };


    // ====================================================================
    // NodeRef — タスク or 条件の「どの種別をどのパラメータで使うか」
    // ====================================================================

    /// @brief NodeRegistry に登録された種別へのハンドル + パラメータ位置。
    /// @details どのタスク/条件を、どのパラメータで実行するか
    /// ITask* / ICondition* はここには持たない。
    /// 実行時に NodeRegistry::GetTask(typeId) で取得する（シングルトン共有）。
    struct NodeRef
    {
        // ノードのタスクのID
        // 例：１番の攻撃タスクを指定する
        NodeTypeID typeId       = ST_INVALID_NODE_TYPE; ///< 種別 ID

        // --- マニュアル（固定設定）の読み込み位置 ---
        // 例：攻撃タスクの威力をOffsetのSizeの領域に書いてあるからそれを見る
        uint32_t   paramOffset  = 0;    ///< paramBlob 内のパラメータ先頭
        uint32_t   paramSize    = 0;    ///< パラメータのバイト数
    };


    // ====================================================================
    // TransitionTrigger — 遷移がいつ評価されるか
    // ====================================================================

    enum class TransitionTrigger : uint8_t
    {
        OnTick       = 0,   ///< 毎フレーム評価（条件チェック）
        OnSucceeded  = 1,   ///< 現フレームのタスクが Succeeded を返したとき
        OnFailed     = 2,   ///< 現フレームのタスクが Failed を返したとき
        OnEvent      = 3,   ///< イベント受信時（イベント種別は condTypeId で識別）
    };


    // ====================================================================
    // Transition — 遷移ルール
    // ====================================================================

    /// @brief 「いつ」「どの条件で」「どの状態へ」遷移するかの1ルール。
    struct Transition
    {
        uint16_t        targetState   = ST_INVALID;           ///< 遷移先 states インデックス
        TransitionTrigger trigger     = TransitionTrigger::OnTick;
        NodeTypeID      condTypeId    = ST_INVALID_NODE_TYPE; ///< 条件種別（なければ無条件）
        uint32_t        condParamOffset = 0;                  ///< paramBlob 内の条件パラメータ
        uint32_t        condParamSize   = 0;
    };


    // ====================================================================
    // StateTreeAsset — 1種類の敵の「頭脳の設計図」
    // ====================================================================

    /// @brief StateTree の不変データ全体。World Resource として1種別につき1個登録。
    ///
    /// 全 Entity がこれを参照（コピーしない）。
    /// M1 ではハードコードで組み立て、M2 で JSON ローダに置き換える。
    struct StateTreeAsset
    {
        std::string name;  ///< "Goblin", "Boss" など（デバッグ・エディタ用）

        // --- 木の骨格 ---
        // 一つのステートノードのこと
        std::vector<StateNode>  states;       ///< [0] = ルート
        // どのタスクと条件をどういったパラメータで行うか
        std::vector<NodeRef>    tasks;        ///< 全ノードのタスク参照
        std::vector<NodeRef>    conditions;   ///< 全ノードの条件参照
        std::vector<Transition> transitions;  ///< 全ノードの遷移ルール

        // --- パラメータ塊（全ノードのパラメータを連続配置）---
        // paramBlob[NodeRef::paramOffset] がその NodeRef のパラメータ先頭。
        // POD 構造体をそのままコピーして埋め込む。
        std::vector<uint8_t>    paramBlob;

        // --- 名前ブロブ（デバッグ・エディタ用、実行時ロジックでは使わない）---
        std::vector<char>       nameBlob;


        // ================================================================
        // 組み立てヘルパー（M1 ハードコード用・M2 では JSON ローダが使う）
        // ================================================================

        /// @brief 新しい状態ノードを追加して states インデックスを返す。
        uint16_t AddState(const std::string& stateName,
                          uint16_t parent = ST_INVALID)
        {
            const uint16_t idx = static_cast<uint16_t>(states.size());
            StateNode node;
            node.parent = parent;

            // 名前をブロブに追記
            node.nameOffset = static_cast<uint16_t>(nameBlob.size());
            node.nameLength = static_cast<uint16_t>(stateName.size());
            nameBlob.insert(nameBlob.end(), stateName.begin(), stateName.end());

            // 親の firstChild / childCount を更新
            if (parent != ST_INVALID)
            {
                StateNode& parentNode = states[parent];

                if (parentNode.firstChild == ST_INVALID)
                {
                    parentNode.firstChild = idx;
                }
                else
                {
                    // 末尾の兄弟を探して nextSibling を繋ぐ
                    uint16_t sibling = parentNode.firstChild;
                    while (states[sibling].nextSibling != ST_INVALID)
                        sibling = states[sibling].nextSibling;
                    states[sibling].nextSibling = idx;
                }
                ++parentNode.childCount;
            }

            states.push_back(node);
            return idx;
        }

        /// @brief paramBlob にパラメータをコピーし、そのオフセットを返す。
        /// @code
        ///   MoveToTargetTask::Params p{ .speed = 4.0f };
        ///   uint32_t offset = asset.AddParams(p);
        /// @endcode
        template<typename TParams>
        uint32_t AddParams(const TParams& params)
        {
            const uint32_t offset = static_cast<uint32_t>(paramBlob.size());
            const uint8_t* src = reinterpret_cast<const uint8_t*>(&params);
            paramBlob.insert(paramBlob.end(), src, src + sizeof(TParams));
            return offset;
        }

        /// @brief タスクを状態に追加する。
        /// @param stateIdx AddState() で取得したインデックス
        /// @param typeId   NodeRegistry から取得した種別 ID
        /// @param paramOff AddParams<T>() で取得したオフセット
        /// @param paramSz  sizeof(Params)
        void AddTask(uint16_t stateIdx,
                     NodeTypeID typeId,
                     uint32_t paramOff, uint32_t paramSz)
        {
            StateNode& st = states[stateIdx];
            if (st.taskCount == 0)
                st.taskBegin = static_cast<uint16_t>(tasks.size());

            NodeRef ref;
            ref.typeId          = typeId;
            ref.paramOffset     = paramOff;
            ref.paramSize       = paramSz;

            tasks.push_back(ref);
            ++st.taskCount;
        }

        /// @brief 入場条件を状態に追加する。
        void AddEnterCondition(uint16_t stateIdx,
                               NodeTypeID typeId,
                               uint32_t paramOff, uint32_t paramSz)
        {
            StateNode& st = states[stateIdx];
            if (st.enterCondCount == 0)
                st.enterCondBegin = static_cast<uint16_t>(conditions.size());

            NodeRef ref;
            ref.typeId      = typeId;
            ref.paramOffset = paramOff;
            ref.paramSize   = paramSz;
            conditions.push_back(ref);
            ++st.enterCondCount;
        }

        /// @brief 遷移ルールを状態に追加する。
        void AddTransition(uint16_t stateIdx,
                           uint16_t targetStateIdx,
                           TransitionTrigger trigger,
                           NodeTypeID condTypeId       = ST_INVALID_NODE_TYPE,
                           uint32_t   condParamOff     = 0,
                           uint32_t   condParamSz      = 0)
        {
            StateNode& st = states[stateIdx];
            if (st.transCount == 0)
                st.transBegin = static_cast<uint16_t>(transitions.size());

            Transition tr;
            tr.targetState    = targetStateIdx;
            tr.trigger        = trigger;
            tr.condTypeId     = condTypeId;
            tr.condParamOffset = condParamOff;
            tr.condParamSize   = condParamSz;
            transitions.push_back(tr);
            ++st.transCount;
        }

        // --- デバッグヘルパー ---

        /// @brief states インデックスから状態名を返す（エディタ・ログ用）。
        std::string GetStateName(uint16_t idx) const
        {
            if (idx >= states.size()) return "(invalid)";
            const auto& st = states[idx];
            if (st.nameLength == 0) return "(unnamed)";
            return std::string(nameBlob.data() + st.nameOffset, st.nameLength);
        }
    };

} // namespace CCL::AI
