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
#include "AllBattlegroundScript.h"
#include "ArenaTeam.h"
#include "ArenaTeamMgr.h"
#include "ArenaTeamScript.h"
#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "BattlegroundQueue.h"
#include "Chat.h"
#include "Config.h"
#include "Creature.h"
#include "CreatureScript.h"
#include "Log.h"
#include "Player.h"
#include "PlayerScript.h"
#include "ScriptMgr.h"
#include "ScriptedGossip.h"
#include "World.h"
#include "WorldScript.h"

enum SoloQueueGossipActions
{
    SOLOQ_GOSSIP_JOIN_2V2_RATED     = 1,
    SOLOQ_GOSSIP_JOIN_3V3_RATED     = 2,
    SOLOQ_GOSSIP_JOIN_2V2_SKIRMISH  = 3,
    SOLOQ_GOSSIP_JOIN_3V3_SKIRMISH  = 4,
    SOLOQ_GOSSIP_LEAVE_QUEUE        = 5,
    SOLOQ_GOSSIP_STATS              = 6
};

// Store pre-match ratings for reward calculation
static std::unordered_map<uint32, std::pair<uint32, uint32>> bgArenaTeamsRating; // bgInstanceId -> {team1Rating, team2Rating}

/*
 * ============================================================
 * NPC Gossip - Solo Queue Master
 * ============================================================
 */
class NpcSoloQueue : public CreatureScript
{
public:
    NpcSoloQueue() : CreatureScript("NpcSoloQueue") { }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        if (!player || !creature)
            return true;

        ClearGossipMenuFor(player);

        bool inQueue = player->InBattlegroundQueueForBattlegroundQueueType(
                           static_cast<BattlegroundQueueTypeId>(BATTLEGROUND_QUEUE_2v2_SOLO)) ||
                       player->InBattlegroundQueueForBattlegroundQueueType(
                           static_cast<BattlegroundQueueTypeId>(BATTLEGROUND_QUEUE_3v3_SOLO));

        if (inQueue)
        {
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "|TInterface/Icons/Ability_Rogue_Sprint:20|t Leave Queue",
                             GOSSIP_SENDER_MAIN, SOLOQ_GOSSIP_LEAVE_QUEUE);
        }
        else
        {
            if (sSoloQueue->IsEnabled2v2())
            {
                AddGossipItemFor(player, GOSSIP_ICON_BATTLE,
                    "|TInterface/Icons/Achievement_Arena_2v2_7:20|t Queue 2v2 Solo (Rated)",
                    GOSSIP_SENDER_MAIN, SOLOQ_GOSSIP_JOIN_2V2_RATED);
                AddGossipItemFor(player, GOSSIP_ICON_CHAT,
                    "|TInterface/Icons/Achievement_Arena_2v2_1:20|t Queue 2v2 Solo (Skirmish)",
                    GOSSIP_SENDER_MAIN, SOLOQ_GOSSIP_JOIN_2V2_SKIRMISH);
            }

            if (sSoloQueue->IsEnabled3v3())
            {
                AddGossipItemFor(player, GOSSIP_ICON_BATTLE,
                    "|TInterface/Icons/Achievement_Arena_3v3_7:20|t Queue 3v3 Solo (Rated)",
                    GOSSIP_SENDER_MAIN, SOLOQ_GOSSIP_JOIN_3V3_RATED);
                AddGossipItemFor(player, GOSSIP_ICON_CHAT,
                    "|TInterface/Icons/Achievement_Arena_3v3_1:20|t Queue 3v3 Solo (Skirmish)",
                    GOSSIP_SENDER_MAIN, SOLOQ_GOSSIP_JOIN_3V3_SKIRMISH);
            }
        }

        AddGossipItemFor(player, GOSSIP_ICON_TABARD,
            "|TInterface/Icons/Achievement_Arena_2v2_3:20|t My Statistics",
            GOSSIP_SENDER_MAIN, SOLOQ_GOSSIP_STATS);

        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature);
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 /*sender*/, uint32 action) override
    {
        if (!player)
            return true;

        CloseGossipMenuFor(player);

        switch (action)
        {
            case SOLOQ_GOSSIP_JOIN_2V2_RATED:
                JoinQueue(player, ARENA_TYPE_2v2_SOLO, true);
                break;
            case SOLOQ_GOSSIP_JOIN_3V3_RATED:
                JoinQueue(player, ARENA_TYPE_3v3_SOLO, true);
                break;
            case SOLOQ_GOSSIP_JOIN_2V2_SKIRMISH:
                JoinQueue(player, ARENA_TYPE_2v2_SOLO, false);
                break;
            case SOLOQ_GOSSIP_JOIN_3V3_SKIRMISH:
                JoinQueue(player, ARENA_TYPE_3v3_SOLO, false);
                break;
            case SOLOQ_GOSSIP_LEAVE_QUEUE:
                LeaveQueue(player);
                break;
            case SOLOQ_GOSSIP_STATS:
                ShowStats(player);
                break;
        }

        return true;
    }

private:
    void JoinQueue(Player* player, uint8 arenaType, bool isRated)
    {
        // Validation checks
        if (player->GetLevel() < sSoloQueue->GetMinLevel())
        {
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cffff0000[Solo Queue]|r You need to be level {} to join.", sSoloQueue->GetMinLevel());
            return;
        }

        if (player->IsInCombat())
        {
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cffff0000[Solo Queue]|r You cannot queue while in combat.");
            return;
        }

        if (player->HasAura(SPELL_DESERTER))
        {
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cffff0000[Solo Queue]|r You cannot queue while you have the Deserter debuff.");
            return;
        }

        if (player->InBattlegroundQueue())
        {
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cffff0000[Solo Queue]|r You are already in a queue.");
            return;
        }

        if (player->GetGroup())
        {
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cffff0000[Solo Queue]|r You must leave your group to join Solo Queue.");
            return;
        }

        if (!sSoloQueue->ArenaCheckFullEquipAndTalents(player))
        {
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cffff0000[Solo Queue]|r You must have all equipment slots filled and no unspent talent points.");
            return;
        }

        // Get bracket
        PvPDifficultyEntry const* bracketEntry = sBattlegroundMgr->GetBattlegroundBracketByLevel(
            BATTLEGROUND_AA, player->GetLevel());
        if (!bracketEntry)
            return;

        // Get rating for rated matches
        uint32 arenaRating = 0;
        uint32 matchmakerRating = sWorld->getIntConfig(CONFIG_ARENA_START_MATCHMAKER_RATING);
        uint32 arenaTeamId = 0;

        if (isRated)
        {
            uint8 slot = (arenaType == ARENA_TYPE_2v2_SOLO) ? ARENA_SLOT_SOLO_2v2 : ARENA_SLOT_SOLO_3v3;
            arenaTeamId = player->GetArenaTeamId(slot);

            if (arenaTeamId)
            {
                ArenaTeam* at = sArenaTeamMgr->GetArenaTeamById(arenaTeamId);
                if (at)
                {
                    arenaRating = at->GetRating();
                    matchmakerRating = at->GetRating(); // Use team rating as MMR
                }
            }
        }

        // Add to queue
        BattlegroundQueueTypeId bgQueueTypeId = static_cast<BattlegroundQueueTypeId>(
            (arenaType == ARENA_TYPE_2v2_SOLO) ? BATTLEGROUND_QUEUE_2v2_SOLO : BATTLEGROUND_QUEUE_3v3_SOLO);

        BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);
        bgQueue.AddGroup(player, nullptr, BATTLEGROUND_AA, bracketEntry,
                         arenaType, isRated, false, arenaRating, matchmakerRating, arenaTeamId, 0);

        uint32 queueSlot = player->AddBattlegroundQueueId(bgQueueTypeId);

        WorldPacket data;
        sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, nullptr, queueSlot, STATUS_WAIT_QUEUE,
            0, 0, arenaType, TEAM_NEUTRAL);
        player->GetSession()->SendPacket(&data);

        std::string mode = (arenaType == ARENA_TYPE_2v2_SOLO) ? "2v2" : "3v3";
        std::string type = isRated ? "Rated" : "Skirmish";
        ChatHandler(player->GetSession()).PSendSysMessage(
            "|cff00ff00[Solo Queue]|r You have joined the {} {} queue. Rating: {}",
            mode, type, arenaRating);
    }

    void LeaveQueue(Player* player)
    {
        for (uint8 queueTypeId : { BATTLEGROUND_QUEUE_2v2_SOLO, BATTLEGROUND_QUEUE_3v3_SOLO })
        {
            auto bgQueueTypeId = static_cast<BattlegroundQueueTypeId>(queueTypeId);
            if (player->InBattlegroundQueueForBattlegroundQueueType(bgQueueTypeId))
            {
                BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);
                bgQueue.RemovePlayer(player->GetGUID(), false);
                player->RemoveBattlegroundQueueId(bgQueueTypeId);

                WorldPacket data;
                sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, nullptr,
                    player->GetBattlegroundQueueIndex(bgQueueTypeId), STATUS_NONE, 0, 0, 0, TEAM_NEUTRAL);
                player->GetSession()->SendPacket(&data);
            }
        }

        ChatHandler(player->GetSession()).PSendSysMessage(
            "|cff00ff00[Solo Queue]|r You have left the queue.");
    }

    void ShowStats(Player* player)
    {
        ChatHandler handler(player->GetSession());
        handler.PSendSysMessage("|cff00ff00=== Solo Queue Statistics ===|r");

        // 2v2 Stats
        uint32 teamId2v2 = player->GetArenaTeamId(ARENA_SLOT_SOLO_2v2);
        if (teamId2v2)
        {
            ArenaTeam* at = sArenaTeamMgr->GetArenaTeamById(teamId2v2);
            if (at)
            {
                handler.PSendSysMessage("|cffffff00[2v2 Solo]|r Rating: {} | W/L: {}/{}",
                    at->GetRating(), at->GetStats().WeekWins, at->GetStats().WeekGames - at->GetStats().WeekWins);
            }
        }
        else
        {
            handler.PSendSysMessage("|cffffff00[2v2 Solo]|r No team yet.");
        }

        // 3v3 Stats
        uint32 teamId3v3 = player->GetArenaTeamId(ARENA_SLOT_SOLO_3v3);
        if (teamId3v3)
        {
            ArenaTeam* at = sArenaTeamMgr->GetArenaTeamById(teamId3v3);
            if (at)
            {
                handler.PSendSysMessage("|cffffff00[3v3 Solo]|r Rating: {} | W/L: {}/{}",
                    at->GetRating(), at->GetStats().WeekWins, at->GetStats().WeekGames - at->GetStats().WeekWins);
            }
        }
        else
        {
            handler.PSendSysMessage("|cffffff00[3v3 Solo]|r No team yet.");
        }
    }
};

/*
 * ============================================================
 * AllBattlegroundScript - Queue Update & Match Rewards
 * ============================================================
 */
class SoloQueueBGScript : public AllBattlegroundScript
{
public:
    SoloQueueBGScript() : AllBattlegroundScript("SoloQueueBGScript") { }

    void OnQueueUpdate(BattlegroundQueue* queue, uint32 /*diff*/, BattlegroundTypeId bgTypeId,
                       BattlegroundBracketId bracket_id, uint8 arenaType,
                       bool isRated, uint32 /*arenaRating*/) override
    {
        if (bgTypeId != BATTLEGROUND_AA)
            return;

        if (arenaType == ARENA_TYPE_2v2_SOLO && sSoloQueue->IsEnabled2v2())
            sSoloQueue->CheckSoloArena(queue, bracket_id, arenaType, isRated);
        else if (arenaType == ARENA_TYPE_3v3_SOLO && sSoloQueue->IsEnabled3v3())
            sSoloQueue->CheckSoloArena(queue, bracket_id, arenaType, isRated);
    }

    bool OnQueueUpdateValidity(BattlegroundQueue* /*queue*/, uint32 /*diff*/,
                               BattlegroundTypeId /*bgTypeId*/, BattlegroundBracketId /*bracket_id*/,
                               uint8 arenaType, bool /*isRated*/, uint32 /*arenaRating*/) override
    {
        // Return false to prevent core from processing our custom queue types
        if (arenaType == ARENA_TYPE_2v2_SOLO || arenaType == ARENA_TYPE_3v3_SOLO)
            return false;
        return true;
    }

    void OnBattlegroundEndReward(Battleground* bg, Player* player, TeamId winnerTeamId) override
    {
        if (!bg || !bg->isArena())
            return;

        uint8 arenaType = bg->GetArenaType();
        if (arenaType != ARENA_TYPE_2v2_SOLO && arenaType != ARENA_TYPE_3v3_SOLO)
            return;

        if (!bg->isRated())
            return;

        uint8 slot = (arenaType == ARENA_TYPE_2v2_SOLO) ? ARENA_SLOT_SOLO_2v2 : ARENA_SLOT_SOLO_3v3;
        uint32 arenaTeamId = player->GetArenaTeamId(slot);
        ArenaTeam* team = sArenaTeamMgr->GetArenaTeamById(arenaTeamId);
        if (!team)
            return;

        // Get opponent's rating from stored data
        auto it = bgArenaTeamsRating.find(bg->GetInstanceID());
        uint32 opponentMMR = team->GetRating(); // fallback

        if (it != bgArenaTeamsRating.end())
        {
            TeamId playerTeam = player->GetBgTeamId();
            opponentMMR = (playerTeam == TEAM_ALLIANCE) ? it->second.second : it->second.first;
        }

        if (player->GetBgTeamId() == winnerTeamId)
        {
            team->MemberWon(player, opponentMMR, GetRatingMod(team->GetRating(), opponentMMR, true));
        }
        else
        {
            team->MemberLost(player, opponentMMR, GetRatingMod(team->GetRating(), opponentMMR, false));
        }

        team->SaveToDB();
    }

    void OnBattlegroundDestroy(Battleground* bg) override
    {
        if (bg)
            bgArenaTeamsRating.erase(bg->GetInstanceID());
    }

    void OnBattlegroundRemovePlayerAtLeave(Battleground* bg, Player* player) override
    {
        if (!bg || !bg->isArena() || !player)
            return;

        uint8 arenaType = bg->GetArenaType();
        if (arenaType != ARENA_TYPE_2v2_SOLO && arenaType != ARENA_TYPE_3v3_SOLO)
            return;

        // Apply deserter debuff
        if (sSoloQueue->CastDeserterOnLeave())
            player->CastSpell(player, SPELL_DESERTER, true);

        // Apply rating penalty
        if (bg->isRated())
            sSoloQueue->CountAsLoss(player, arenaType);
    }

private:
    int32 GetRatingMod(uint32 ownRating, uint32 opponentRating, bool won)
    {
        float chance = 1.0f / (1.0f + std::exp((float)(opponentRating - ownRating) / 400.0f));
        float kFactor = 32.0f;

        if (won)
            return static_cast<int32>(std::ceil(kFactor * (1.0f - chance)));
        else
            return -static_cast<int32>(std::ceil(kFactor * chance));
    }
};

/*
 * ============================================================
 * ArenaTeamScript - Register custom arena types
 * ============================================================
 */
class SoloQueueArenaTeamScript : public ArenaTeamScript
{
public:
    SoloQueueArenaTeamScript() : ArenaTeamScript("SoloQueueArenaTeamScript") { }

    void OnGetSlotByType(const uint32 type, uint8& slot) override
    {
        if (type == ARENA_TEAM_SOLO_2v2)
            slot = ARENA_SLOT_SOLO_2v2;
        else if (type == ARENA_TEAM_SOLO_3v3)
            slot = ARENA_SLOT_SOLO_3v3;
    }

    void OnTypeIDToQueueID(const BattlegroundTypeId /*bgTypeId*/, const uint8 arenaType,
                           uint32& queueTypeID) override
    {
        if (arenaType == ARENA_TYPE_2v2_SOLO)
            queueTypeID = BATTLEGROUND_QUEUE_2v2_SOLO;
        else if (arenaType == ARENA_TYPE_3v3_SOLO)
            queueTypeID = BATTLEGROUND_QUEUE_3v3_SOLO;
    }

    void OnQueueIdToArenaType(const BattlegroundQueueTypeId bgQueueTypeId, uint8& arenaType) override
    {
        if (bgQueueTypeId == static_cast<BattlegroundQueueTypeId>(BATTLEGROUND_QUEUE_2v2_SOLO))
            arenaType = ARENA_TYPE_2v2_SOLO;
        else if (bgQueueTypeId == static_cast<BattlegroundQueueTypeId>(BATTLEGROUND_QUEUE_3v3_SOLO))
            arenaType = ARENA_TYPE_3v3_SOLO;
    }

    void OnSetArenaMaxPlayersPerTeam(const uint8 arenaType, uint32& maxPlayers) override
    {
        if (arenaType == ARENA_TYPE_2v2_SOLO)
            maxPlayers = 4; // 2v2 = 4 total
        else if (arenaType == ARENA_TYPE_3v3_SOLO)
            maxPlayers = 6; // 3v3 = 6 total
    }
};

/*
 * ============================================================
 * WorldScript - Config Loader
 * ============================================================
 */
class SoloQueueWorldScript : public WorldScript
{
public:
    SoloQueueWorldScript() : WorldScript("SoloQueueWorldScript") { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        sSoloQueue->LoadConfig();
    }
};

/*
 * ============================================================
 * PlayerScript - Login message & Leave penalty
 * ============================================================
 */
class SoloQueuePlayerScript : public PlayerScript
{
public:
    SoloQueuePlayerScript() : PlayerScript("SoloQueuePlayerScript") { }

    void OnPlayerLogin(Player* player) override
    {
        if (!player)
            return;

        if (!sSoloQueue->ShowLoginMessage())
            return;

        if (!sSoloQueue->IsEnabled2v2() && !sSoloQueue->IsEnabled3v3())
            return;

        std::string modes;
        if (sSoloQueue->IsEnabled2v2()) modes += "2v2";
        if (sSoloQueue->IsEnabled2v2() && sSoloQueue->IsEnabled3v3()) modes += " & ";
        if (sSoloQueue->IsEnabled3v3()) modes += "3v3";

        ChatHandler(player->GetSession()).PSendSysMessage(
            "|cff00ff00[Solo Queue]|r Arena Solo Queue ({}) is available! Find the Solo Queue NPC to join.",
            modes);
    }
};

/*
 * ============================================================
 * Script Registration
 * ============================================================
 */
void AddSC_soloqueue()
{
    new NpcSoloQueue();
    new SoloQueueBGScript();
    new SoloQueueArenaTeamScript();
    new SoloQueueWorldScript();
    new SoloQueuePlayerScript();
}
