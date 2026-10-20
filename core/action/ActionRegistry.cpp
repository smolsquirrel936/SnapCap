#include "core/action/ActionRegistry.h"

#include <QDebug>

#include "core/action/KeySequenceCodec.h"

#include <yaml-cpp/yaml.h>

void ActionRegistry::registerAction(ActionId id, std::function<void()> handler)
{
    m_handlers.insert(id, std::move(handler));
}

bool ActionRegistry::dispatch(ActionId id) const
{
    const auto it = m_handlers.constFind(id);
    if (it == m_handlers.constEnd() || !it.value())
        return false;
    it.value()();
    return true;
}

void ActionRegistry::bindKey(ActionId id, const QString& sequence, KeyContext ctx)
{
    const QString norm = KeySequenceCodec::normalize(sequence);
    if (norm.isEmpty())
        return;                                 // empty/invalid -> leave unbound
    m_byKey[static_cast<int>(ctx)].insert(norm, id);
    m_byId.insert(id, norm);
}

QString ActionRegistry::keyFor(ActionId id) const
{
    return m_byId.value(id);
}

std::optional<ActionId> ActionRegistry::actionFor(const QString& sequence, KeyContext ctx) const
{
    const QString norm = KeySequenceCodec::normalize(sequence);
    const auto cit = m_byKey.constFind(static_cast<int>(ctx));
    if (cit == m_byKey.constEnd())
        return std::nullopt;
    const auto it = cit->constFind(norm);
    if (it == cit->constEnd())
        return std::nullopt;
    return it.value();
}

std::optional<ActionId> ActionRegistry::actionIdFromName(const QString& name)
{
    static const QHash<QString, ActionId> map = {
        {"snip", ActionId::Snip},
        {"snipCopy", ActionId::SnipCopy},
        {"customSnip", ActionId::CustomSnip},
        {"snipActiveWindow", ActionId::SnipActiveWindow},
        {"paste", ActionId::Paste},
        {"hideAll", ActionId::HideAllPins},
        {"closeAll", ActionId::CloseAllPins},
        {"destroyAll", ActionId::DestroyAllPins},
        {"clickThrough", ActionId::ToggleClickThrough},
        {"pickColor", ActionId::PickColor},
        {"scanBarcode", ActionId::ScanBarcode},
        {"preferences", ActionId::OpenPreferences},
        {"quit", ActionId::Quit},

        {"snip_copy", ActionId::Snip_Copy},
        {"snip_save", ActionId::Snip_Save},
        {"snip_quicksave", ActionId::Snip_QuickSave},
        {"snip_pin", ActionId::Snip_Pin},
        {"snip_toggleDetect", ActionId::Snip_ToggleDetect},
        {"snip_fullscreen", ActionId::Snip_Fullscreen},
        {"snip_repeatLast", ActionId::Snip_RepeatLast},
        {"snip_histPrev", ActionId::Snip_HistPrev},
        {"snip_histNext", ActionId::Snip_HistNext},
        {"snip_toggleCursor", ActionId::Snip_ToggleCursor},
        {"snip_refresh", ActionId::Snip_Refresh},

        {"edit_undo", ActionId::Edit_Undo},
        {"edit_redo", ActionId::Edit_Redo},
        {"edit_clearAll", ActionId::Edit_ClearAll},
    };
    const auto it = map.constFind(name);
    if (it == map.constEnd())
        return std::nullopt;
    return it.value();
}

bool ActionRegistry::loadDefaults(const QString& yamlPath)
{
    static const QHash<QString, KeyContext> ctxByName = {
        {"global", KeyContext::Global},
        {"snip", KeyContext::Snip},
        {"pin", KeyContext::Pin},
        {"edit", KeyContext::Edit},
    };

    try {
        const YAML::Node root = YAML::LoadFile(yamlPath.toStdString());
        for (auto git = root.begin(); git != root.end(); ++git) {
            const QString group = QString::fromStdString(git->first.as<std::string>());
            if (!ctxByName.contains(group))
                continue;
            const KeyContext ctx = ctxByName.value(group);
            const YAML::Node& items = git->second;
            if (!items.IsMap())
                continue;
            for (auto it = items.begin(); it != items.end(); ++it) {
                const QString name = QString::fromStdString(it->first.as<std::string>());
                const QString seq = QString::fromStdString(it->second.as<std::string>());
                const auto id = actionIdFromName(name);
                if (!id) {
                    qWarning() << "[keymap] unknown action name:" << name;
                    continue;
                }
                if (!seq.isEmpty())
                    bindKey(*id, seq, ctx);     // empty = intentionally unbound
            }
        }
        return true;
    } catch (const std::exception& e) {
        qWarning() << "[keymap] failed to parse" << yamlPath << ":" << e.what();
        return false;
    }
}
