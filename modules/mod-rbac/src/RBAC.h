/*
 * mod-rbac
 * Module-based Role-Based Access Control for AzerothCore
 *
 * Implements granular command permissions using the module_string
 * composite-key pattern (module, id) to namespace permissions per module.
 * Uses AllCommandScript hooks for grant/deny without core changes.
 */

#ifndef MOD_RBAC_H
#define MOD_RBAC_H

#include "Common.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Config.h"

#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>

// Composite key: (module, permissionId) — same pattern as module_string
using RBACPermKey = std::pair<std::string, uint32>;

// ============================================================================
// Data Structures
// ============================================================================

struct RBACPermission
{
    std::string module;
    uint32 id = 0;
    std::string name;
    std::string linkedCommand; // e.g., "ban account" (empty if not command-linked)
};

struct RBACRole
{
    uint32 id = 0;
    std::string name;
    std::set<RBACPermKey> permissions;
};

struct RBACAccountData
{
    uint32 accountId = 0;
    uint8 securityLevel = 0;

    // Explicit per-account grants and denies
    std::set<RBACPermKey> grantedPerms;
    std::set<RBACPermKey> deniedPerms;

    // Assigned role IDs
    std::set<uint32> roleIds;

    // Pre-computed effective permissions (grants + role grants - denies)
    std::set<RBACPermKey> effectiveGranted;
    std::set<RBACPermKey> effectiveDenied;

    void Recalculate(
        std::map<uint32, RBACRole> const& allRoles,
        std::map<uint8, std::set<RBACPermKey>> const& defaultPerms);
};

// ============================================================================
// Manager Singleton
// ============================================================================

class RBACMgr
{
public:
    static RBACMgr* instance();

    // Permission check results
    enum class PermResult
    {
        GRANTED,    // Explicitly granted (override security level)
        DENIED,     // Explicitly denied (override security level)
        NOT_FOUND   // No RBAC entry, fall through to security level check
    };

    // Configuration
    void LoadConfig();
    bool IsEnabled() const { return _enabled; }

    // Database loading
    void LoadPermissions();
    void LoadRoles();
    void LoadRolePermissions();
    void LoadDefaultPerms();
    void LoadAccountPerms();
    void LoadAccountRoles();
    void LoadAll();
    void Reload();

    // Permission checks (core API)
    PermResult HasPermission(uint32 accountId,
        std::string const& module, uint32 permId) const;
    PermResult HasCommandPermission(uint32 accountId,
        std::string const& commandName) const;

    // Lookups
    RBACPermission const* GetPermission(
        std::string const& module, uint32 id) const;
    RBACPermission const* GetPermissionByCommand(
        std::string const& commandName) const;
    RBACRole const* GetRole(uint32 roleId) const;
    RBACAccountData const* GetAccountData(uint32 accountId) const;

    // All permissions/roles (for listing)
    std::map<RBACPermKey, RBACPermission> const& GetAllPermissions() const { return _permissions; }
    std::map<uint32, RBACRole> const& GetAllRoles() const { return _roles; }

    // Mutations (for GM commands) - write to DB and update cache
    bool GrantPermission(uint32 accountId,
        std::string const& module, uint32 permId, int realmId = -1);
    bool DenyPermission(uint32 accountId,
        std::string const& module, uint32 permId, int realmId = -1);
    bool RevokePermission(uint32 accountId,
        std::string const& module, uint32 permId, int realmId = -1);
    bool AssignRole(uint32 accountId, uint32 roleId, int realmId = -1);
    bool UnassignRole(uint32 accountId, uint32 roleId, int realmId = -1);
    uint32 CreateRole(std::string const& name);
    bool AddPermToRole(uint32 roleId,
        std::string const& module, uint32 permId);

private:
    RBACMgr() = default;
    ~RBACMgr() = default;

    void RecalculateAccount(uint32 accountId);

    // Config
    bool _enabled = false;
    bool _denyOverrides = true;
    bool _logChecks = false;

    // Permission definitions: (module, id) -> RBACPermission
    std::map<RBACPermKey, RBACPermission> _permissions;

    // Reverse index: lowercase command name -> permission key
    std::unordered_map<std::string, RBACPermKey> _commandIndex;

    // Roles: roleId -> RBACRole
    std::map<uint32, RBACRole> _roles;

    // Account data: accountId -> RBACAccountData
    std::unordered_map<uint32, RBACAccountData> _accountData;

    // Default permissions per security level
    std::map<uint8, std::set<RBACPermKey>> _defaultPerms;
};

#define sRBACMgr RBACMgr::instance()

#endif // MOD_RBAC_H
