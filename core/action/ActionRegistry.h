#pragma once

#include <QHash>
#include <QString>
#include <functional>
#include <optional>

#include "core/action/ActionId.h"

// Context disambiguates context-local keys: e.g. Ctrl+A means "fullscreen" while
// snipping but "select all pins" on a pin. Global hotkeys use KeyContext::Global.
enum class KeyContext { Global, Snip, Pin, Edit };

// The single command bus: maps ActionId -> handler and (context, key) -> ActionId.
// See claude-plan/Architecture-FileMap.md and Implementation-Plan.md.
class ActionRegistry {
public:
    // Handlers
    void registerAction(ActionId id, std::function<void()> handler);
    bool dispatch(ActionId id) const;          // false if no handler bound

    // Keymap
    void bindKey(ActionId id, const QString& sequence, KeyContext ctx);
    QString keyFor(ActionId id) const;         // "" if unbound
    std::optional<ActionId> actionFor(const QString& sequence, KeyContext ctx) const;

    // Load default bindings from res/keymap.default.yaml (groups: global/snip/pin/edit).
    bool loadDefaults(const QString& yamlPath);

    // Names used in keymap.default.yaml -> ActionId (e.g. "snip" -> ActionId::Snip).
    static std::optional<ActionId> actionIdFromName(const QString& name);

private:
    QHash<ActionId, std::function<void()>> m_handlers;
    QHash<int, QHash<QString, ActionId>> m_byKey;   // int(KeyContext) -> (normKey -> id)
    QHash<ActionId, QString> m_byId;                // id -> canonical key string
};
