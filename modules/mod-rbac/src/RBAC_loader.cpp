/*
 * mod-rbac — Module Loader
 */

// From RBAC_WorldScript.cpp
void AddRBACWorldScripts();
// From RBAC_CommandHook.cpp
void AddRBACCommandHookScripts();
// From RBAC_Commands.cpp
void AddRBACCommands();

void Addmod_rbacScripts()
{
    AddRBACWorldScripts();
    AddRBACCommandHookScripts();
    AddRBACCommands();
}
