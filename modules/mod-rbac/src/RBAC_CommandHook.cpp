/*
 * mod-rbac — Command Hook Script
 * Uses AllCommandScript hooks to intercept command permission checks.
 *
 * CRITICAL HOOK LOGIC:
 *
 * OnBeforeIsInvokerVisible (ChatCommand.cpp:509):
 *   Hook returns FALSE → IsInvokerVisible returns TRUE → GRANT access
 *   Hook returns TRUE  → normal security level check proceeds
 *
 * OnTryExecuteCommand (ChatCommand.cpp:325):
 *   Hook returns FALSE → command execution is BLOCKED
 *   Hook returns TRUE  → normal execution proceeds
 */

#include "RBAC.h"
#include "ScriptMgr.h"
#include "Chat.h"
#include "WorldSession.h"

class RBAC_CommandHookScript : public AllCommandScript
{
public:
    RBAC_CommandHookScript() : AllCommandScript("RBAC_CommandHookScript",
        {ALLCOMMANDHOOK_ON_TRY_EXECUTE_COMMAND,
         ALLCOMMANDHOOK_ON_BEFORE_IS_INVOKER_VISIBLE})
    { }

    /*
     * Called BEFORE IsInvokerVisible (at ChatCommand.cpp:325).
     * Used to DENY execution even if the player has the security level.
     *
     * Return false → BLOCK execution
     * Return true  → proceed normally
     */
    bool OnTryExecuteCommand(ChatHandler& handler, std::string_view cmdStr) override
    {
        if (!sRBACMgr->IsEnabled())
            return true; // Module disabled, pass through

        if (handler.IsConsole())
            return true; // Console always allowed

        WorldSession* session = handler.GetSession();
        if (!session)
            return true;

        uint32 accountId = session->GetAccountId();

        // Extract command name from the raw command string
        std::string cmdName = ExtractCommandName(cmdStr);
        if (cmdName.empty())
            return true;

        auto result = sRBACMgr->HasCommandPermission(accountId, cmdName);

        if (result == RBACMgr::PermResult::DENIED)
        {
            handler.SendSysMessage("|cffff0000[RBAC]|r You do not have permission to use this command.");
            handler.SetSentErrorMessage(true);
            return false; // BLOCK execution
        }

        return true; // GRANTED or NOT_FOUND → proceed normally
    }

    /*
     * Called INSIDE IsInvokerVisible (at ChatCommand.cpp:509).
     * Used to GRANT visibility/access to commands the player's security
     * level wouldn't normally allow.
     *
     * Return false → GRANT access (bypass security level check)
     * Return true  → proceed with normal security level check
     */
    bool OnBeforeIsInvokerVisible(
        std::string name,
        Acore::Impl::ChatCommands::CommandPermissions /*permissions*/,
        ChatHandler const& who) override
    {
        if (!sRBACMgr->IsEnabled())
            return true; // Module disabled, normal check

        if (who.IsConsole())
            return true; // Console always visible

        // Need to const_cast because GetSession() is non-const on ChatHandler
        WorldSession* session = const_cast<ChatHandler&>(who).GetSession();
        if (!session)
            return true;

        uint32 accountId = session->GetAccountId();

        auto result = sRBACMgr->HasCommandPermission(accountId, name);

        if (result == RBACMgr::PermResult::GRANTED)
        {
            // Player has explicit RBAC grant for this command.
            // Return false → bypasses security level check → GRANTS access
            return false;
        }

        // NOT_FOUND or DENIED → fall through to normal security level check
        // Note: DENIED is handled in OnTryExecuteCommand instead,
        // because this hook cannot deny visibility for users with
        // sufficient security level.
        return true;
    }

private:
    /*
     * Extract the command name portion from raw command text.
     * Commands can have multi-word names (e.g., "ban account player123").
     * We try progressively shorter prefixes against the permission index
     * to find the longest matching command name.
     */
    static std::string ExtractCommandName(std::string_view cmdStr)
    {
        std::string str(cmdStr);

        // Trim whitespace
        while (!str.empty() && str.front() == ' ')
            str.erase(str.begin());
        while (!str.empty() && str.back() == ' ')
            str.pop_back();

        if (str.empty())
            return "";

        // Try longest prefix first, then progressively shorter
        std::string attempt = str;
        while (!attempt.empty())
        {
            if (sRBACMgr->GetPermissionByCommand(attempt))
                return attempt;

            // Remove last token (word)
            auto pos = attempt.rfind(' ');
            if (pos == std::string::npos)
                break;
            attempt = attempt.substr(0, pos);
        }

        // If no match in permissions, return the first word as fallback
        auto spacePos = str.find(' ');
        if (spacePos != std::string::npos)
            return str.substr(0, spacePos);

        return str;
    }
};

void AddRBACCommandHookScripts()
{
    new RBAC_CommandHookScript();
}
