/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "soloqueue.h"
#include "ArenaTeam.h"
#include "ArenaTeamMgr.h"
#include "BattlegroundMgr.h"
#include "BattlegroundQueue.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "CommandScript.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "World.h"

using namespace Acore::ChatCommands;

class SoloQueueCommandScript : public CommandScript
{
public:
    SoloQueueCommandScript() : CommandScript("SoloQueueCommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable soloqSubCommands =
        {
            { "2v2",   HandleQueue2v2,  SEC_PLAYER, Console::No },
            { "3v3",   HandleQueue3v3,  SEC_PLAYER, Console::No },
            { "leave", HandleLeave,     SEC_PLAYER, Console::No },
            { "stats", HandleStats,     SEC_PLAYER, Console::No },
        };

        static ChatCommandTable commandTable =
        {
            { "soloq", soloqSubCommands },
        };

        return commandTable;
    }

    static bool HandleQueue2v2(ChatHandler* handler)
    {
        Player* player = handler->GetSession()->GetPlayer();
        if (!player)
            return false;

        if (!sSoloQueue->IsEnabled2v2())
        {
            handler->PSendSysMessage("|cffff0000[Solo Queue]|r 2v2 Solo Queue is disabled.");
            return true;
        }

        return QueuePlayer(handler, player, ARENA_TYPE_2v2_SOLO);
    }

    static bool HandleQueue3v3(ChatHandler* handler)
    {
        Player* player = handler->GetSession()->GetPlayer();
        if (!player)
            return false;

        if (!sSoloQueue->IsEnabled3v3())
        {
            handler->PSendSysMessage("|cffff0000[Solo Queue]|r 3v3 Solo Queue is disabled.");
            return true;
        }

        return QueuePlayer(handler, player, ARENA_TYPE_3v3_SOLO);
    }

    static bool HandleLeave(ChatHandler* handler)
    {
        Player* player = handler->GetSession()->GetPlayer();
        if (!player)
            return false;

        bool removed = false;
        for (uint8 queueTypeId : { BATTLEGROUND_QUEUE_2v2_SOLO, BATTLEGROUND_QUEUE_3v3_SOLO })
        {
            auto bgQueueTypeId = static_cast<BattlegroundQueueTypeId>(queueTypeId);
            if (player->InBattlegroundQueueForBattlegroundQueueType(bgQueueTypeId))
            {
                BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);
                bgQueue.RemovePlayer(player->GetGUID(), false);
                player->RemoveBattlegroundQueueId(bgQueueTypeId);
                removed = true;
            }
        }

        if (removed)
            handler->PSendSysMessage("|cff00ff00[Solo Queue]|r You have left the queue.");
        else
            handler->PSendSysMessage("|cffff0000[Solo Queue]|r You are not in any solo queue.");

        return true;
    }

    static bool HandleStats(ChatHandler* handler)
    {
        Player* player = handler->GetSession()->GetPlayer();
        if (!player)
            return false;

        handler->PSendSysMessage("|cff00ff00=== Solo Queue Statistics ===|r");

        // 2v2
        uint32 teamId = player->GetArenaTeamId(ARENA_SLOT_SOLO_2v2);
        if (teamId)
        {
            ArenaTeam* at = sArenaTeamMgr->GetArenaTeamById(teamId);
            if (at)
                handler->PSendSysMessage("|cffffff00[2v2 Solo]|r Rating: {} | Season: {}-{}",
                    at->GetRating(), at->GetStats().SeasonWins,
                    at->GetStats().SeasonGames - at->GetStats().SeasonWins);
        }
        else
            handler->PSendSysMessage("|cffffff00[2v2 Solo]|r No team.");

        // 3v3
        teamId = player->GetArenaTeamId(ARENA_SLOT_SOLO_3v3);
        if (teamId)
        {
            ArenaTeam* at = sArenaTeamMgr->GetArenaTeamById(teamId);
            if (at)
                handler->PSendSysMessage("|cffffff00[3v3 Solo]|r Rating: {} | Season: {}-{}",
                    at->GetRating(), at->GetStats().SeasonWins,
                    at->GetStats().SeasonGames - at->GetStats().SeasonWins);
        }
        else
            handler->PSendSysMessage("|cffffff00[3v3 Solo]|r No team.");

        return true;
    }

private:
    static bool QueuePlayer(ChatHandler* handler, Player* player, uint8 arenaType)
    {
        if (player->GetLevel() < sSoloQueue->GetMinLevel())
        {
            handler->PSendSysMessage("|cffff0000[Solo Queue]|r You need to be level {}.", sSoloQueue->GetMinLevel());
            return true;
        }

        if (player->IsInCombat())
        {
            handler->PSendSysMessage("|cffff0000[Solo Queue]|r Cannot queue while in combat.");
            return true;
        }

        if (player->HasAura(SPELL_DESERTER))
        {
            handler->PSendSysMessage("|cffff0000[Solo Queue]|r Cannot queue with Deserter debuff.");
            return true;
        }

        if (player->InBattlegroundQueue())
        {
            handler->PSendSysMessage("|cffff0000[Solo Queue]|r Already in a queue.");
            return true;
        }

        if (player->GetGroup())
        {
            handler->PSendSysMessage("|cffff0000[Solo Queue]|r Leave your group first.");
            return true;
        }

        PvPDifficultyEntry const* bracketEntry = sBattlegroundMgr->GetBattlegroundBracketByLevel(
            BATTLEGROUND_AA, player->GetLevel());
        if (!bracketEntry)
            return true;

        uint32 arenaRating = 0;
        uint32 matchmakerRating = sWorld->getIntConfig(CONFIG_ARENA_START_MATCHMAKER_RATING);
        uint8 slot = (arenaType == ARENA_TYPE_2v2_SOLO) ? ARENA_SLOT_SOLO_2v2 : ARENA_SLOT_SOLO_3v3;
        uint32 arenaTeamId = player->GetArenaTeamId(slot);

        if (arenaTeamId)
        {
            ArenaTeam* at = sArenaTeamMgr->GetArenaTeamById(arenaTeamId);
            if (at)
            {
                arenaRating = at->GetRating();
                matchmakerRating = at->GetRating(); // Use team rating as MMR
            }
        }

        BattlegroundQueueTypeId bgQueueTypeId = static_cast<BattlegroundQueueTypeId>(
            (arenaType == ARENA_TYPE_2v2_SOLO) ? BATTLEGROUND_QUEUE_2v2_SOLO : BATTLEGROUND_QUEUE_3v3_SOLO);

        BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);
        bgQueue.AddGroup(player, nullptr, BATTLEGROUND_AA, bracketEntry,
                         arenaType, true, false, arenaRating, matchmakerRating, arenaTeamId, 0);

        uint32 queueSlot = player->AddBattlegroundQueueId(bgQueueTypeId);

        WorldPacket data;
        sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, nullptr, queueSlot, STATUS_WAIT_QUEUE,
            0, 0, arenaType, TEAM_NEUTRAL);
        player->GetSession()->SendPacket(&data);

        std::string mode = (arenaType == ARENA_TYPE_2v2_SOLO) ? "2v2" : "3v3";
        handler->PSendSysMessage("|cff00ff00[Solo Queue]|r Joined {} rated queue. Rating: {}", mode, arenaRating);

        return true;
    }
};

void AddSC_soloqueue_commandscript()
{
    new SoloQueueCommandScript();
}
