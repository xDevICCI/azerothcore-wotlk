/*
 * mod-rbac — RBACMgr implementation
 * Loading, permission checking, and mutation logic
 */

#include "RBAC.h"
#include "World.h"
#include "AccountMgr.h"
#include "Realm.h"

// ============================================================================
// Singleton
// ============================================================================

RBACMgr* RBACMgr::instance()
{
    static RBACMgr inst;
    return &inst;
}

// ============================================================================
// RBACAccountData
// ============================================================================

void RBACAccountData::Recalculate(
    std::map<uint32, RBACRole> const& allRoles,
    std::map<uint8, std::set<RBACPermKey>> const& defaultPerms)
{
    effectiveGranted.clear();
    effectiveDenied.clear();

    // 1. Add default permissions for this security level
    auto defIt = defaultPerms.find(securityLevel);
    if (defIt != defaultPerms.end())
    {
        for (auto const& key : defIt->second)
            effectiveGranted.insert(key);
    }

    // 2. Add explicit grants
    for (auto const& key : grantedPerms)
        effectiveGranted.insert(key);

    // 3. Add grants from assigned roles
    for (uint32 roleId : roleIds)
    {
        auto roleIt = allRoles.find(roleId);
        if (roleIt != allRoles.end())
        {
            for (auto const& permKey : roleIt->second.permissions)
                effectiveGranted.insert(permKey);
        }
    }

    // 4. Apply explicit denies (overrides everything)
    effectiveDenied = deniedPerms;
    for (auto const& denied : effectiveDenied)
        effectiveGranted.erase(denied);
}

// ============================================================================
// Configuration
// ============================================================================

void RBACMgr::LoadConfig()
{
    _enabled = sConfigMgr->GetOption<bool>("RBAC.Enable", false);
    _denyOverrides = sConfigMgr->GetOption<bool>("RBAC.DenyOverridesGrant", true);
    _logChecks = sConfigMgr->GetOption<bool>("RBAC.LogPermissionChecks", false);

    LOG_INFO("module", ">> mod-rbac: Configuration loaded (Enabled: {}, DenyOverrides: {})",
        _enabled ? "Yes" : "No", _denyOverrides ? "Yes" : "No");
}

// ============================================================================
// Database Loading
// ============================================================================

void RBACMgr::LoadPermissions()
{
    uint32 oldMSTime = getMSTime();
    _permissions.clear();
    _commandIndex.clear();

    QueryResult result = WorldDatabase.Query(
        "SELECT module, id, name, linked_command FROM rbac_permissions ORDER BY module, id");

    if (!result)
    {
        LOG_WARN("module", ">> mod-rbac: Loaded 0 permissions. Table `rbac_permissions` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        RBACPermission perm;
        perm.module = fields[0].Get<std::string>();
        perm.id = fields[1].Get<uint32>();
        perm.name = fields[2].Get<std::string>();
        perm.linkedCommand = fields[3].Get<std::string>();

        RBACPermKey key = std::make_pair(perm.module, perm.id);
        _permissions[key] = perm;

        // Build reverse command index (lowercase)
        if (!perm.linkedCommand.empty())
        {
            std::string cmdLower = perm.linkedCommand;
            std::transform(cmdLower.begin(), cmdLower.end(), cmdLower.begin(), ::tolower);
            _commandIndex[cmdLower] = key;
        }

        count++;
    } while (result->NextRow());

    LOG_INFO("module", ">> mod-rbac: Loaded {} permissions ({} command-linked) in {} ms",
        count, _commandIndex.size(), GetMSTimeDiffToNow(oldMSTime));
}

void RBACMgr::LoadRoles()
{
    uint32 oldMSTime = getMSTime();
    _roles.clear();

    QueryResult result = WorldDatabase.Query(
        "SELECT id, name FROM rbac_roles ORDER BY id");

    if (!result)
    {
        LOG_INFO("module", ">> mod-rbac: Loaded 0 roles.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        RBACRole role;
        role.id = fields[0].Get<uint32>();
        role.name = fields[1].Get<std::string>();

        _roles[role.id] = role;
        count++;
    } while (result->NextRow());

    LOG_INFO("module", ">> mod-rbac: Loaded {} roles in {} ms",
        count, GetMSTimeDiffToNow(oldMSTime));
}

void RBACMgr::LoadRolePermissions()
{
    uint32 oldMSTime = getMSTime();

    QueryResult result = WorldDatabase.Query(
        "SELECT roleId, permModule, permId FROM rbac_role_permissions ORDER BY roleId");

    if (!result)
    {
        LOG_INFO("module", ">> mod-rbac: Loaded 0 role-permission mappings.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 roleId = fields[0].Get<uint32>();
        std::string permModule = fields[1].Get<std::string>();
        uint32 permId = fields[2].Get<uint32>();

        auto roleIt = _roles.find(roleId);
        if (roleIt == _roles.end())
        {
            LOG_ERROR("module", "mod-rbac: Role {} in rbac_role_permissions does not exist, skipped.", roleId);
            continue;
        }

        roleIt->second.permissions.insert(std::make_pair(permModule, permId));
        count++;
    } while (result->NextRow());

    LOG_INFO("module", ">> mod-rbac: Loaded {} role-permission mappings in {} ms",
        count, GetMSTimeDiffToNow(oldMSTime));
}

void RBACMgr::LoadDefaultPerms()
{
    uint32 oldMSTime = getMSTime();
    _defaultPerms.clear();

    QueryResult result = WorldDatabase.Query(
        "SELECT securityLevel, permModule, permId FROM rbac_default_permissions ORDER BY securityLevel");

    if (!result)
    {
        LOG_INFO("module", ">> mod-rbac: Loaded 0 default permissions.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint8 secLevel = fields[0].Get<uint8>();
        std::string permModule = fields[1].Get<std::string>();
        uint32 permId = fields[2].Get<uint32>();

        _defaultPerms[secLevel].insert(std::make_pair(permModule, permId));
        count++;
    } while (result->NextRow());

    LOG_INFO("module", ">> mod-rbac: Loaded {} default permissions in {} ms",
        count, GetMSTimeDiffToNow(oldMSTime));
}

void RBACMgr::LoadAccountPerms()
{
    uint32 oldMSTime = getMSTime();
    _accountData.clear();

    int32 realmId = realm.Id.Realm;

    QueryResult result = LoginDatabase.Query(
        "SELECT accountId, permModule, permId, granted FROM rbac_account_permissions "
        "WHERE realmId = -1 OR realmId = {}", realmId);

    if (!result)
    {
        LOG_INFO("module", ">> mod-rbac: Loaded 0 account permissions.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 accountId = fields[0].Get<uint32>();
        std::string permModule = fields[1].Get<std::string>();
        uint32 permId = fields[2].Get<uint32>();
        bool granted = fields[3].Get<bool>();

        RBACAccountData& data = _accountData[accountId];
        data.accountId = accountId;

        RBACPermKey key = std::make_pair(permModule, permId);
        if (granted)
            data.grantedPerms.insert(key);
        else
            data.deniedPerms.insert(key);

        count++;
    } while (result->NextRow());

    LOG_INFO("module", ">> mod-rbac: Loaded {} account permissions in {} ms",
        count, GetMSTimeDiffToNow(oldMSTime));
}

void RBACMgr::LoadAccountRoles()
{
    uint32 oldMSTime = getMSTime();

    int32 realmId = realm.Id.Realm;

    QueryResult result = LoginDatabase.Query(
        "SELECT accountId, roleId FROM rbac_account_roles "
        "WHERE realmId = -1 OR realmId = {}", realmId);

    if (!result)
    {
        LOG_INFO("module", ">> mod-rbac: Loaded 0 account role assignments.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 accountId = fields[0].Get<uint32>();
        uint32 roleId = fields[1].Get<uint32>();

        _accountData[accountId].accountId = accountId;
        _accountData[accountId].roleIds.insert(roleId);
        count++;
    } while (result->NextRow());

    LOG_INFO("module", ">> mod-rbac: Loaded {} account role assignments in {} ms",
        count, GetMSTimeDiffToNow(oldMSTime));
}

void RBACMgr::LoadAll()
{
    LOG_INFO("module", ">> mod-rbac: Loading RBAC data...");
    LoadPermissions();
    LoadRoles();
    LoadRolePermissions();
    LoadDefaultPerms();
    LoadAccountPerms();
    LoadAccountRoles();

    // Load security levels and recalculate effective permissions
    for (auto& [accountId, data] : _accountData)
    {
        data.securityLevel = static_cast<uint8>(
            AccountMgr::GetSecurity(accountId, realm.Id.Realm));
        data.Recalculate(_roles, _defaultPerms);
    }

    LOG_INFO("module", ">> mod-rbac: RBAC data loaded ({} permissions, {} roles, {} accounts)",
        _permissions.size(), _roles.size(), _accountData.size());
}

void RBACMgr::Reload()
{
    LOG_INFO("module", ">> mod-rbac: Reloading RBAC data...");
    LoadAll();
}

// ============================================================================
// Permission Checks
// ============================================================================

RBACMgr::PermResult RBACMgr::HasPermission(uint32 accountId,
    std::string const& module, uint32 permId) const
{
    RBACPermKey key = std::make_pair(module, permId);

    auto accIt = _accountData.find(accountId);
    if (accIt != _accountData.end())
    {
        auto const& data = accIt->second;

        if (data.effectiveDenied.count(key))
        {
            if (_logChecks)
                LOG_DEBUG("module", "mod-rbac: Account {} DENIED permission ({}, {})",
                    accountId, module, permId);
            return PermResult::DENIED;
        }

        if (data.effectiveGranted.count(key))
        {
            if (_logChecks)
                LOG_DEBUG("module", "mod-rbac: Account {} GRANTED permission ({}, {})",
                    accountId, module, permId);
            return PermResult::GRANTED;
        }
    }

    return PermResult::NOT_FOUND;
}

RBACMgr::PermResult RBACMgr::HasCommandPermission(uint32 accountId,
    std::string const& commandName) const
{
    // Lowercase the command name for lookup
    std::string cmdLower = commandName;
    std::transform(cmdLower.begin(), cmdLower.end(), cmdLower.begin(), ::tolower);

    auto cmdIt = _commandIndex.find(cmdLower);
    if (cmdIt == _commandIndex.end())
        return PermResult::NOT_FOUND;

    return HasPermission(accountId, cmdIt->second.first, cmdIt->second.second);
}

// ============================================================================
// Lookups
// ============================================================================

RBACPermission const* RBACMgr::GetPermission(
    std::string const& module, uint32 id) const
{
    RBACPermKey key = std::make_pair(module, id);
    auto it = _permissions.find(key);
    return it != _permissions.end() ? &it->second : nullptr;
}

RBACPermission const* RBACMgr::GetPermissionByCommand(
    std::string const& commandName) const
{
    std::string cmdLower = commandName;
    std::transform(cmdLower.begin(), cmdLower.end(), cmdLower.begin(), ::tolower);

    auto cmdIt = _commandIndex.find(cmdLower);
    if (cmdIt == _commandIndex.end())
        return nullptr;

    auto permIt = _permissions.find(cmdIt->second);
    return permIt != _permissions.end() ? &permIt->second : nullptr;
}

RBACRole const* RBACMgr::GetRole(uint32 roleId) const
{
    auto it = _roles.find(roleId);
    return it != _roles.end() ? &it->second : nullptr;
}

RBACAccountData const* RBACMgr::GetAccountData(uint32 accountId) const
{
    auto it = _accountData.find(accountId);
    return it != _accountData.end() ? &it->second : nullptr;
}

// ============================================================================
// Mutations (write to DB + update cache)
// ============================================================================

void RBACMgr::RecalculateAccount(uint32 accountId)
{
    auto it = _accountData.find(accountId);
    if (it != _accountData.end())
    {
        it->second.securityLevel = static_cast<uint8>(
            AccountMgr::GetSecurity(accountId, realm.Id.Realm));
        it->second.Recalculate(_roles, _defaultPerms);
    }
}

bool RBACMgr::GrantPermission(uint32 accountId,
    std::string const& module, uint32 permId, int realmId)
{
    // Verify permission exists
    if (!GetPermission(module, permId))
        return false;

    LoginDatabase.Execute(
        "REPLACE INTO rbac_account_permissions (accountId, permModule, permId, granted, realmId) "
        "VALUES ({}, '{}', {}, 1, {})",
        accountId, module, permId, realmId);

    // Update cache
    RBACAccountData& data = _accountData[accountId];
    data.accountId = accountId;
    RBACPermKey key = std::make_pair(module, permId);
    data.grantedPerms.insert(key);
    data.deniedPerms.erase(key);
    RecalculateAccount(accountId);

    return true;
}

bool RBACMgr::DenyPermission(uint32 accountId,
    std::string const& module, uint32 permId, int realmId)
{
    if (!GetPermission(module, permId))
        return false;

    LoginDatabase.Execute(
        "REPLACE INTO rbac_account_permissions (accountId, permModule, permId, granted, realmId) "
        "VALUES ({}, '{}', {}, 0, {})",
        accountId, module, permId, realmId);

    RBACAccountData& data = _accountData[accountId];
    data.accountId = accountId;
    RBACPermKey key = std::make_pair(module, permId);
    data.deniedPerms.insert(key);
    data.grantedPerms.erase(key);
    RecalculateAccount(accountId);

    return true;
}

bool RBACMgr::RevokePermission(uint32 accountId,
    std::string const& module, uint32 permId, int realmId)
{
    LoginDatabase.Execute(
        "DELETE FROM rbac_account_permissions "
        "WHERE accountId = {} AND permModule = '{}' AND permId = {} AND realmId = {}",
        accountId, module, permId, realmId);

    auto it = _accountData.find(accountId);
    if (it != _accountData.end())
    {
        RBACPermKey key = std::make_pair(module, permId);
        it->second.grantedPerms.erase(key);
        it->second.deniedPerms.erase(key);
        RecalculateAccount(accountId);
    }

    return true;
}

bool RBACMgr::AssignRole(uint32 accountId, uint32 roleId, int realmId)
{
    if (!GetRole(roleId))
        return false;

    LoginDatabase.Execute(
        "REPLACE INTO rbac_account_roles (accountId, roleId, realmId) "
        "VALUES ({}, {}, {})",
        accountId, roleId, realmId);

    RBACAccountData& data = _accountData[accountId];
    data.accountId = accountId;
    data.roleIds.insert(roleId);
    RecalculateAccount(accountId);

    return true;
}

bool RBACMgr::UnassignRole(uint32 accountId, uint32 roleId, int realmId)
{
    LoginDatabase.Execute(
        "DELETE FROM rbac_account_roles "
        "WHERE accountId = {} AND roleId = {} AND realmId = {}",
        accountId, roleId, realmId);

    auto it = _accountData.find(accountId);
    if (it != _accountData.end())
    {
        it->second.roleIds.erase(roleId);
        RecalculateAccount(accountId);
    }

    return true;
}

uint32 RBACMgr::CreateRole(std::string const& name)
{
    WorldDatabase.Execute(
        "INSERT INTO rbac_roles (name) VALUES ('{}')", name);

    // Get the auto-incremented ID
    QueryResult result = WorldDatabase.Query(
        "SELECT id FROM rbac_roles WHERE name = '{}'", name);

    if (!result)
        return 0;

    uint32 roleId = (*result)[0].Get<uint32>();

    RBACRole role;
    role.id = roleId;
    role.name = name;
    _roles[roleId] = role;

    return roleId;
}

bool RBACMgr::AddPermToRole(uint32 roleId,
    std::string const& module, uint32 permId)
{
    if (!GetRole(roleId) || !GetPermission(module, permId))
        return false;

    WorldDatabase.Execute(
        "REPLACE INTO rbac_role_permissions (roleId, permModule, permId) "
        "VALUES ({}, '{}', {})",
        roleId, module, permId);

    _roles[roleId].permissions.insert(std::make_pair(module, permId));

    // Recalculate all accounts with this role
    for (auto& [accountId, data] : _accountData)
    {
        if (data.roleIds.count(roleId))
            RecalculateAccount(accountId);
    }

    return true;
}
