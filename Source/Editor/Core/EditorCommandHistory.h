#pragma once
#include <vector>
#include <memory>
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Core/Logger.h"

// =========================================================
// 1. 命令書の基本インターフェース
// =========================================================
class IEditorCommand {
public:
    virtual ~IEditorCommand() = default;
    virtual void Execute() = 0; // 実行 (Redo)
    virtual void Undo() = 0;    // 取り消し (Undo)
};

// =========================================================
// 2. 履歴マネージャー (どこからでも呼べるシングルトン)
// =========================================================
class EditorCommandHistory {
public:
    static EditorCommandHistory& Instance() {
        static EditorCommandHistory instance;
        return instance;
    }

    // 履歴の上限。超えたぶんは古い方から捨てる。
    //
    // 以前は無制限だった。BTGraphChangeCommand は1編集ごとにグラフ全体の JSON を
    // 2枚（編集前・編集後）抱えるため、長時間の編集セッションで際限なく積み上がる。
    static constexpr size_t kMaxHistory = 128;

    void ExecuteCommand(std::unique_ptr<IEditorCommand> cmd) {
        cmd->Execute();
        m_undoStack.push_back(std::move(cmd));
        m_redoStack.clear(); // 新しい操作をしたら未来は消える

        if (m_undoStack.size() > kMaxHistory)
            m_undoStack.erase(m_undoStack.begin());

        CCL_LOG_INFO(LogCategory::Editor, "[CommandHistory] Command Executed. UndoStack Size: %zu", m_undoStack.size());
    }

    // 履歴を全部捨てる。シーンを切り替えるときに必ず呼ぶこと。
    //
    // ChangeComponentCommand は World* を生ポインタで、対象を EntityID で持つ。
    // シーン切替は SceneManager::Clear() が currentScene ごと delete するので、
    // 履歴を残したまま Ctrl+Z を押すと【解放済みの World】を書きに行く。
    // 仮に World が生きていたとしても、EntityID の世代は World ごとに 1 から
    // 振り直される（CCL_World.cpp の m_entityGenerations）ため、旧シーンの ID が
    // 新シーンの無関係なエンティティに命中する。どちらの意味でも残してはいけない。
    void Clear() {
        const size_t dropped = m_undoStack.size() + m_redoStack.size();
        m_undoStack.clear();
        m_redoStack.clear();
        if (dropped > 0)
            CCL_LOG_INFO(LogCategory::Editor, "[CommandHistory] 履歴を破棄しました (%zu 件)", dropped);
    }

    void Undo() {
        if (m_undoStack.empty()) {
            CCL_LOG_WARN(LogCategory::Editor, "[CommandHistory] Undo failed: Stack is empty.");
            return;
        }
        auto cmd = std::move(m_undoStack.back());
        m_undoStack.pop_back();

        cmd->Undo();
        m_redoStack.push_back(std::move(cmd));

        CCL_LOG_SUCCESS(LogCategory::Editor, "[CommandHistory] UNDO Performed. Remaining Undo: %zu, Redo: %zu", m_undoStack.size(), m_redoStack.size());
    }

    void Redo() {
        if (m_redoStack.empty()) {
            CCL_LOG_WARN(LogCategory::Editor, "[CommandHistory] Redo failed: Stack is empty.");
            return;
        }

        auto cmd = std::move(m_redoStack.back());
        m_redoStack.pop_back();

        cmd->Execute();
        m_undoStack.push_back(std::move(cmd));

        CCL_LOG_SUCCESS(LogCategory::Editor, "[CommandHistory] REDO Performed. Remaining Undo: %zu, Redo: %zu", m_undoStack.size(), m_redoStack.size());
    }

private:
    std::vector<std::unique_ptr<IEditorCommand>> m_undoStack;
    std::vector<std::unique_ptr<IEditorCommand>> m_redoStack;
};

// =========================================================
// 3. 汎用コマンド：任意のコンポーネント(T)の変更前後を保持して Undo/Redo する
// =========================================================
template <typename T>
class ChangeComponentCommand : public IEditorCommand {
private:
    CCL::ECS::World* m_world;
    CCL::ECS::EntityID m_entity;
    T m_oldData;
    T m_newData;

public:
    ChangeComponentCommand(CCL::ECS::World* world, CCL::ECS::EntityID entity, const T& oldData, const T& newData)
        : m_world(world), m_entity(entity), m_oldData(oldData), m_newData(newData) {
    }

    void Execute() override {
        if (auto* comp = m_world->GetComponent<T>(m_entity)) {
            *comp = m_newData;
        }
    }

    void Undo() override {
        if (auto* comp = m_world->GetComponent<T>(m_entity)) {
            *comp = m_oldData;
        }
    }
};