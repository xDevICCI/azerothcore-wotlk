/*
 * mod-rbac — World Script
 * Handles configuration loading and startup initialization.
 */

#include "RBAC.h"
#include "ScriptMgr.h"

class RBAC_WorldScript : public WorldScript
{
public:
    RBAC_WorldScript() : WorldScript("RBAC_WorldScript",
        {WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP})
    { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        sRBACMgr->LoadConfig();
    }

    void OnStartup() override
    {
        if (sRBACMgr->IsEnabled())
            sRBACMgr->LoadAll();
    }
};

void AddRBACWorldScripts()
{
    new RBAC_WorldScript();
}
