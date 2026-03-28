#include "PluginManager.hpp"
//
//
//

#include "../Utils/Common.h"


// Implementation of: class Plugin
Plugin::Plugin(const char* name) noexcept
    : name  (name)
{ }

Plugin::Plugin(const std::string& name) noexcept
    : name  (name)
{ }

Plugin::~Plugin()
{ }

std::string Plugin::GetName() const noexcept
{
    return name;
}


// Implementation of: class PluginManager
void PluginManager::EnablePlugin(Plugin* plugin)
{
    tlsys_assert(plugin, "PluginManager attempted to enable a NULL plugin instance.");

    PluginEvent::PreEnable(this, plugin).Fire();
    plugin->OnEnable();
    PluginEvent::PostEnable(this, plugin).Fire();

    plugins[plugin->GetName()] = plugin;
}

Plugin* PluginManager::DisablePlugin(const std::string& name)
{
    auto iter = plugins.find(name);
    printf("disable plugin: %s\n", name.c_str());
    if (iter == end())
        return nullptr;

    Plugin* plugin = iter->second;

    PluginEvent::PreDisable(this, plugin).Fire();
    plugin->OnDisable();
    PluginEvent::PostDisable(this, plugin).Fire();

    plugins.erase(iter);

    return plugin;
}

void PluginManager::DisableAll(std::function<void(Plugin*)> callback)
{
    while (!plugins.empty())
        DisablePlugin(plugins.begin()->first);
}

Plugin* PluginManager::GetPlugin(const std::string& name) noexcept
{
    auto iter = plugins.find(name);

    if (iter == end())
        return nullptr;

    return iter->second;
}

const Plugin* PluginManager::GetPlugin(const std::string& name) const noexcept
{
    auto iter = plugins.find(name);

    if (iter == end())
        return nullptr;

    return iter->second;
}

bool PluginManager::HasPlugin(const std::string& name) const noexcept
{
    return plugins.find(name) != end();
}

PluginManager::iterator PluginManager::begin() noexcept
{
    return plugins.begin();
}

PluginManager::const_iterator PluginManager::begin() const noexcept
{
    return plugins.begin();
}

PluginManager::iterator PluginManager::end() noexcept
{
    return plugins.end();
}

PluginManager::const_iterator PluginManager::end() const noexcept
{
    return plugins.end();
}


// Implementation of: class PluginEvent::Base
PluginEvent::Base::Base(PluginManager* manager, Plugin* plugin) noexcept
    : manager   (manager)
    , plugin    (plugin)
{ }

PluginManager* PluginEvent::Base::GetManager() const noexcept
{
    return manager;
}

Plugin* PluginEvent::Base::GetPlugin() const noexcept
{
    return plugin;
}

// Implementation of: class PluginEvent::PreEnable
PluginEvent::PreEnable::PreEnable(PluginManager* manager, Plugin* plugin) noexcept
    : Base  (manager, plugin)
{ }

// Implementation of: class PluginEvent::PostEnable
PluginEvent::PostEnable::PostEnable(PluginManager* manager, Plugin* plugin) noexcept
    : Base  (manager, plugin)
{ }

// Implementation of: class PluginEvent::PreDisable
PluginEvent::PreDisable::PreDisable(PluginManager* manager, Plugin* plugin) noexcept
    : Base  (manager, plugin)
{ }

// Implementation of: class PluginEvent::PostDisable
PluginEvent::PostDisable::PostDisable(PluginManager* manager, Plugin* plugin) noexcept
    : Base  (manager, plugin)
{ }
