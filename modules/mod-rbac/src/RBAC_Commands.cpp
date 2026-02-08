/*
 * mod-rbac — GM Commands
 * .rbac account grant/deny/revoke/list
 * .rbac role create/addperm/assign/unassign/list
 * .rbac permlist / .rbac reload
 */

#include "RBAC.h"
#include "ScriptMgr.h"
#include "Chat.h"
#include "Player.h"
#include "WorldSession.h"

using namespace Acore::ChatCommands;

class RBAC_CommandScript : public CommandScript
{
public:
    RBAC_CommandScript() : CommandScript("RBAC_CommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable rbacAccountTable =
        {
            { "grant",  HandleRbacAccountGrant,   SEC_ADMINISTRATOR, Console::Yes },
            { "deny",   HandleRbacAccountDeny,    SEC_ADMINISTRATOR, Console::Yes },
            { "revoke", HandleRbacAccountRevoke,  SEC_ADMINISTRATOR, Console::Yes },
            { "list",   HandleRbacAccountList,    SEC_ADMINISTRATOR, Console::Yes },
        };

        static ChatCommandTable rbacRoleTable =
        {
            { "create",   HandleRbacRoleCreate,   SEC_ADMINISTRATOR, Console::Yes },
            { "addperm",  HandleRbacRoleAddPerm,  SEC_ADMINISTRATOR, Console::Yes },
            { "assign",   HandleRbacRoleAssign,   SEC_ADMINISTRATOR, Console::Yes },
            { "unassign", HandleRbacRoleUnassign, SEC_ADMINISTRATOR, Console::Yes },
            { "list",     HandleRbacRoleList,     SEC_ADMINISTRATOR, Console::Yes },
        };

        static ChatCommandTable rbacTable =
        {
            { "account",  rbacAccountTable },
            { "role",     rbacRoleTable },
            { "permlist", HandleRbacPermList,  SEC_ADMINISTRATOR, Console::Yes },
            { "reload",   HandleRbacReload,    SEC_ADMINISTRATOR, Console::Yes },
            { "status",   HandleRbacStatus,    SEC_GAMEMASTER,    Console::Yes },
        };

        static ChatCommandTable commandTable =
        {
            { "rbac", rbacTable },
        };

        return commandTable;
    }

    // ====================================================================
    // Account commands
    // ====================================================================

    // .rbac account grant <accountId> <module> <permId>
    static bool HandleRbacAccountGrant(ChatHandler* handler,
        uint32 accountId, std::string module, uint32 permId)
    {
        if (!sRBACMgr->IsEnabled())
        {
            handler->SendSysMessage("RBAC module is disabled.");
            return true;
        }

        if (sRBACMgr->GrantPermission(accountId, module, permId))
        {
            handler->PSendSysMessage("|cff00ff00[RBAC]|r Granted permission (%s, %u) to account %u.",
                module.c_str(), permId, accountId);
        }
        else
        {
            handler->PSendSysMessage("|cffff0000[RBAC]|r Permission (%s, %u) not found.",
                module.c_str(), permId);
        }
        return true;
    }

    // .rbac account deny <accountId> <module> <permId>
    static bool HandleRbacAccountDeny(ChatHandler* handler,
        uint32 accountId, std::string module, uint32 permId)
    {
        if (!sRBACMgr->IsEnabled())
        {
            handler->SendSysMessage("RBAC module is disabled.");
            return true;
        }

        if (sRBACMgr->DenyPermission(accountId, module, permId))
        {
            handler->PSendSysMessage("|cff00ff00[RBAC]|r Denied permission (%s, %u) for account %u.",
                module.c_str(), permId, accountId);
        }
        else
        {
            handler->PSendSysMessage("|cffff0000[RBAC]|r Permission (%s, %u) not found.",
                module.c_str(), permId);
        }
        return true;
    }

    // .rbac account revoke <accountId> <module> <permId>
    static bool HandleRbacAccountRevoke(ChatHandler* handler,
        uint32 accountId, std::string module, uint32 permId)
    {
        if (!sRBACMgr->IsEnabled())
        {
            handler->SendSysMessage("RBAC module is disabled.");
            return true;
        }

        sRBACMgr->RevokePermission(accountId, module, permId);
        handler->PSendSysMessage("|cff00ff00[RBAC]|r Revoked permission (%s, %u) from account %u.",
            module.c_str(), permId, accountId);
        return true;
    }

    // .rbac account list <accountId>
    static bool HandleRbacAccountList(ChatHandler* handler, uint32 accountId)
    {
        if (!sRBACMgr->IsEnabled())
        {
            handler->SendSysMessage("RBAC module is disabled.");
            return true;
        }

        RBACAccountData const* data = sRBACMgr->GetAccountData(accountId);
        if (!data)
        {
            handler->PSendSysMessage("No RBAC data for account %u.", accountId);
            return true;
        }

        handler->PSendSysMessage("=== RBAC for Account %u (SecLevel: %u) ===", accountId, data->securityLevel);

        // Effective grants
        handler->SendSysMessage("|cff00ff00Effective Grants:|r");
        if (data->effectiveGranted.empty())
        {
            handler->SendSysMessage("  (none)");
        }
        else
        {
            for (auto const& key : data->effectiveGranted)
            {
                RBACPermission const* perm = sRBACMgr->GetPermission(key.first, key.second);
                handler->PSendSysMessage("  [%s:%u] %s",
                    key.first.c_str(), key.second,
                    perm ? perm->name.c_str() : "Unknown");
            }
        }

        // Effective denies
        handler->SendSysMessage("|cffff0000Effective Denies:|r");
        if (data->effectiveDenied.empty())
        {
            handler->SendSysMessage("  (none)");
        }
        else
        {
            for (auto const& key : data->effectiveDenied)
            {
                RBACPermission const* perm = sRBACMgr->GetPermission(key.first, key.second);
                handler->PSendSysMessage("  [%s:%u] %s",
                    key.first.c_str(), key.second,
                    perm ? perm->name.c_str() : "Unknown");
            }
        }

        // Roles
        handler->SendSysMessage("|cffffff00Assigned Roles:|r");
        if (data->roleIds.empty())
        {
            handler->SendSysMessage("  (none)");
        }
        else
        {
            for (uint32 roleId : data->roleIds)
            {
                RBACRole const* role = sRBACMgr->GetRole(roleId);
                handler->PSendSysMessage("  [%u] %s",
                    roleId, role ? role->name.c_str() : "Unknown");
            }
        }

        return true;
    }

    // ====================================================================
    // Role commands
    // ====================================================================

    // .rbac role create <name>
    static bool HandleRbacRoleCreate(ChatHandler* handler, Tail name)
    {
        if (!sRBACMgr->IsEnabled())
        {
            handler->SendSysMessage("RBAC module is disabled.");
            return true;
        }

        std::string roleName(name);
        if (roleName.empty())
        {
            handler->SendSysMessage("Usage: .rbac role create <name>");
            return false;
        }

        uint32 roleId = sRBACMgr->CreateRole(roleName);
        if (roleId)
        {
            handler->PSendSysMessage("|cff00ff00[RBAC]|r Created role '%s' with ID %u.",
                roleName.c_str(), roleId);
        }
        else
        {
            handler->SendSysMessage("|cffff0000[RBAC]|r Failed to create role (duplicate name?).");
        }
        return true;
    }

    // .rbac role addperm <roleId> <module> <permId>
    static bool HandleRbacRoleAddPerm(ChatHandler* handler,
        uint32 roleId, std::string module, uint32 permId)
    {
        if (!sRBACMgr->IsEnabled())
        {
            handler->SendSysMessage("RBAC module is disabled.");
            return true;
        }

        if (sRBACMgr->AddPermToRole(roleId, module, permId))
        {
            handler->PSendSysMessage("|cff00ff00[RBAC]|r Added permission (%s, %u) to role %u.",
                module.c_str(), permId, roleId);
        }
        else
        {
            handler->SendSysMessage("|cffff0000[RBAC]|r Role or permission not found.");
        }
        return true;
    }

    // .rbac role assign <accountId> <roleId>
    static bool HandleRbacRoleAssign(ChatHandler* handler,
        uint32 accountId, uint32 roleId)
    {
        if (!sRBACMgr->IsEnabled())
        {
            handler->SendSysMessage("RBAC module is disabled.");
            return true;
        }

        if (sRBACMgr->AssignRole(accountId, roleId))
        {
            handler->PSendSysMessage("|cff00ff00[RBAC]|r Assigned role %u to account %u.",
                roleId, accountId);
        }
        else
        {
            handler->SendSysMessage("|cffff0000[RBAC]|r Role not found.");
        }
        return true;
    }

    // .rbac role unassign <accountId> <roleId>
    static bool HandleRbacRoleUnassign(ChatHandler* handler,
        uint32 accountId, uint32 roleId)
    {
        if (!sRBACMgr->IsEnabled())
        {
            handler->SendSysMessage("RBAC module is disabled.");
            return true;
        }

        sRBACMgr->UnassignRole(accountId, roleId);
        handler->PSendSysMessage("|cff00ff00[RBAC]|r Unassigned role %u from account %u.",
            roleId, accountId);
        return true;
    }

    // .rbac role list
    static bool HandleRbacRoleList(ChatHandler* handler)
    {
        if (!sRBACMgr->IsEnabled())
        {
            handler->SendSysMessage("RBAC module is disabled.");
            return true;
        }

        auto const& roles = sRBACMgr->GetAllRoles();
        if (roles.empty())
        {
            handler->SendSysMessage("No roles defined.");
            return true;
        }

        handler->SendSysMessage("=== RBAC Roles ===");
        for (auto const& [id, role] : roles)
        {
            handler->PSendSysMessage("  [%u] %s (%zu permissions)",
                id, role.name.c_str(), role.permissions.size());
        }
        return true;
    }

    // ====================================================================
    // Utility commands
    // ====================================================================

    // .rbac permlist [module]
    static bool HandleRbacPermList(ChatHandler* handler, Optional<std::string> filterModule)
    {
        if (!sRBACMgr->IsEnabled())
        {
            handler->SendSysMessage("RBAC module is disabled.");
            return true;
        }

        auto const& perms = sRBACMgr->GetAllPermissions();
        if (perms.empty())
        {
            handler->SendSysMessage("No permissions defined.");
            return true;
        }

        handler->SendSysMessage("=== RBAC Permissions ===");
        for (auto const& [key, perm] : perms)
        {
            if (filterModule && *filterModule != perm.module)
                continue;

            handler->PSendSysMessage("  [%s:%u] %s %s",
                perm.module.c_str(), perm.id,
                perm.name.c_str(),
                perm.linkedCommand.empty() ? "" :
                    Acore::StringFormat("-> .{}", perm.linkedCommand).c_str());
        }
        return true;
    }

    // .rbac reload
    static bool HandleRbacReload(ChatHandler* handler)
    {
        sRBACMgr->Reload();
        handler->SendSysMessage("|cff00ff00[RBAC]|r RBAC data reloaded.");
        return true;
    }

    // .rbac status
    static bool HandleRbacStatus(ChatHandler* handler)
    {
        handler->PSendSysMessage("RBAC Module: %s",
            sRBACMgr->IsEnabled() ? "|cff00ff00ENABLED|r" : "|cffff0000DISABLED|r");

        if (sRBACMgr->IsEnabled())
        {
            handler->PSendSysMessage("  Permissions: %zu", sRBACMgr->GetAllPermissions().size());
            handler->PSendSysMessage("  Roles: %zu", sRBACMgr->GetAllRoles().size());
        }
        return true;
    }
};

void AddRBACCommands()
{
    new RBAC_CommandScript();
}
