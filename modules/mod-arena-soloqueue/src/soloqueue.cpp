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
#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "BattlegroundQueue.h"
#include "Chat.h"
#include "Config.h"
#include "DBCStores.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"

SoloQueue* SoloQueue::instance()
{
    static SoloQueue inst;
    return &inst;
}

void SoloQueue::LoadConfig()
{
    _enable2v2 = sConfigMgr->GetOption<bool>("SoloQueue.2v2.Enable", true);
    _enable3v3 = sConfigMgr->GetOption<bool>("SoloQueue.3v3.Enable", true);
    _minLevel = sConfigMgr->GetOption<uint32>("SoloQueue.MinLevel", 80);
    _cost = sConfigMgr->GetOption<uint32>("SoloQueue.Cost", 0);
    _filterTalents = sConfigMgr->GetOption<bool>("SoloQueue.FilterTalents", true);
    _maxRatingDiff = sConfigMgr->GetOption<uint32>("SoloQueue.MaxRatingDifference", 150);
    _deserterOnLeave = sConfigMgr->GetOption<bool>("SoloQueue.CastDeserterOnLeave", true);
    _deserterOnAfk = sConfigMgr->GetOption<bool>("SoloQueue.CastDeserterOnAfk", true);
    _stopIncomplete = sConfigMgr->GetOption<bool>("SoloQueue.StopGameIncomplete", true);
    _leavePenalty = sConfigMgr->GetOption<int32>("SoloQueue.RatingPenalty.Leave", 24);
    _leaveBeforeStartPenalty = sConfigMgr->GetOption<int32>("SoloQueue.RatingPenalty.LeaveBeforeStart", 50);
    _arenaPointsMulti = sConfigMgr->GetOption<float>("SoloQueue.ArenaPointsMulti", 0.8f);
    _showLoginMsg = sConfigMgr->GetOption<bool>("SoloQueue.ShowMessageOnLogin", true);
    _checkEquip = sConfigMgr->GetOption<bool>("SoloQueue.CheckEquipAndTalents", false);
}

SoloQueueTalentCat SoloQueue::GetTalentCategory(Player* player)
{
    if (!player)
        return SOLOQ_MELEE;

    uint8 playerClass = player->getClass();

    // Get talent points in each tree (indices 0, 1, 2)
    uint32 tab0 = 0, tab1 = 0, tab2 = 0;

    // Count talent points per tab
    for (uint32 talentId = 0; talentId < sTalentStore.GetNumRows(); ++talentId)
    {
        TalentEntry const* talentInfo = sTalentStore.LookupEntry(talentId);
        if (!talentInfo)
            continue;

        TalentTabEntry const* talentTabInfo = sTalentTabStore.LookupEntry(talentInfo->TalentTab);
        if (!talentTabInfo)
            continue;

        if (!(talentTabInfo->ClassMask & player->getClassMask()))
            continue;

        for (uint8 rank = MAX_TALENT_RANK; rank > 0; --rank)
        {
            if (talentInfo->RankID[rank - 1] && player->HasSpell(talentInfo->RankID[rank - 1]))
            {
                if (talentTabInfo->tabpage == 0)
                    tab0 += rank;
                else if (talentTabInfo->tabpage == 1)
                    tab1 += rank;
                else
                    tab2 += rank;
                break;
            }
        }
    }

    // Determine primary spec (highest point tree)
    uint8 primaryTab = 0;
    if (tab1 > tab0 && tab1 > tab2) primaryTab = 1;
    else if (tab2 > tab0 && tab2 > tab1) primaryTab = 2;

    // Classification by class + primary talent tree
    switch (playerClass)
    {
        case CLASS_WARRIOR:     // Arms(0), Fury(1), Protection(2) - all melee
            return SOLOQ_MELEE;
        case CLASS_PALADIN:     // Holy(0), Protection(1), Retribution(2)
            return primaryTab == 0 ? SOLOQ_HEALER : SOLOQ_MELEE;
        case CLASS_HUNTER:      // BM(0), MM(1), Surv(2) - all ranged
            return SOLOQ_RANGE;
        case CLASS_ROGUE:       // all melee
            return SOLOQ_MELEE;
        case CLASS_PRIEST:      // Disc(0), Holy(1), Shadow(2)
            return primaryTab == 2 ? SOLOQ_RANGE : SOLOQ_HEALER;
        case CLASS_DEATH_KNIGHT: // all melee
            return SOLOQ_MELEE;
        case CLASS_SHAMAN:      // Ele(0), Enh(1), Resto(2)
            if (primaryTab == 2) return SOLOQ_HEALER;
            return primaryTab == 0 ? SOLOQ_RANGE : SOLOQ_MELEE;
        case CLASS_MAGE:        // all ranged
            return SOLOQ_RANGE;
        case CLASS_WARLOCK:     // all ranged
            return SOLOQ_RANGE;
        case CLASS_DRUID:       // Balance(0), Feral(1), Resto(2)
            if (primaryTab == 2) return SOLOQ_HEALER;
            return primaryTab == 0 ? SOLOQ_RANGE : SOLOQ_MELEE;
        default:
            return SOLOQ_MELEE;
    }
}

bool SoloQueue::ArenaCheckFullEquipAndTalents(Player* player)
{
    if (!_checkEquip)
        return true;

    if (!player)
        return false;

    // Check unspent talent points
    if (player->GetFreeTalentPoints() > 0)
        return false;

    // Check essential equipment slots
    static const uint8 requiredSlots[] = {
        EQUIPMENT_SLOT_HEAD, EQUIPMENT_SLOT_NECK, EQUIPMENT_SLOT_SHOULDERS,
        EQUIPMENT_SLOT_CHEST, EQUIPMENT_SLOT_WAIST, EQUIPMENT_SLOT_LEGS,
        EQUIPMENT_SLOT_FEET, EQUIPMENT_SLOT_WRISTS, EQUIPMENT_SLOT_HANDS,
        EQUIPMENT_SLOT_FINGER1, EQUIPMENT_SLOT_FINGER2, EQUIPMENT_SLOT_TRINKET1,
        EQUIPMENT_SLOT_TRINKET2, EQUIPMENT_SLOT_BACK, EQUIPMENT_SLOT_MAINHAND
    };

    for (uint8 slot : requiredSlots)
    {
        if (!player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            return false;
    }

    return true;
}

bool SoloQueue::CheckSoloArena(BattlegroundQueue* queue, BattlegroundBracketId bracketId,
                                uint8 arenaType, bool isRated)
{
    uint8 playersPerTeam = (arenaType == ARENA_TYPE_2v2_SOLO) ? 2 : 3;
    uint8 totalPlayersNeeded = playersPerTeam * 2;

    // Collect all solo-queued players for this bracket
    // They are in normal alliance/horde queues as individual groups
    std::vector<GroupQueueInfo*> soloPlayers;

    for (uint8 queueType = 0; queueType < BG_QUEUE_MAX; ++queueType)
    {
        for (auto* ginfo : queue->m_QueuedGroups[bracketId][queueType])
        {
            if (ginfo->IsInvitedToBGInstanceGUID)
                continue;
            if (ginfo->Players.size() != 1)
                continue;
            if (ginfo->ArenaType != arenaType)
                continue;

            soloPlayers.push_back(ginfo);
        }
    }

    if (soloPlayers.size() < totalPlayersNeeded)
        return false;

    // Classify players by role if filter enabled
    std::vector<GroupQueueInfo*> healers, melee, ranged;

    for (auto* ginfo : soloPlayers)
    {
        ObjectGuid guid = *ginfo->Players.begin();
        Player* player = ObjectAccessor::FindPlayer(guid);
        if (!player)
            continue;

        if (_filterTalents)
        {
            SoloQueueTalentCat cat = GetTalentCategory(player);
            switch (cat)
            {
                case SOLOQ_HEALER: healers.push_back(ginfo); break;
                case SOLOQ_MELEE:  melee.push_back(ginfo);   break;
                case SOLOQ_RANGE:  ranged.push_back(ginfo);  break;
            }
        }
        else
        {
            // No filter - treat everyone as DPS for simple matching
            melee.push_back(ginfo);
        }
    }

    // Sort all lists by MMR for better matching
    auto sortByMMR = [](GroupQueueInfo* a, GroupQueueInfo* b)
    {
        return a->ArenaMatchmakerRating < b->ArenaMatchmakerRating;
    };
    std::sort(healers.begin(), healers.end(), sortByMMR);
    std::sort(melee.begin(), melee.end(), sortByMMR);
    std::sort(ranged.begin(), ranged.end(), sortByMMR);

    // Merge DPS pool
    std::vector<GroupQueueInfo*> dps;
    dps.insert(dps.end(), melee.begin(), melee.end());
    dps.insert(dps.end(), ranged.begin(), ranged.end());
    std::sort(dps.begin(), dps.end(), sortByMMR);

    // Build two teams
    std::vector<Player*> team1, team2;
    std::vector<GroupQueueInfo*> team1Info, team2Info;

    if (arenaType == ARENA_TYPE_3v3_SOLO)
    {
        // 3v3: Try 1 healer + 2 DPS per team
        if (healers.size() >= 2 && dps.size() >= 4)
        {
            // Team 1: healer[0] + dps[0] + dps[2]
            // Team 2: healer[1] + dps[1] + dps[3]
            team1Info = { healers[0], dps[0], dps[2] };
            team2Info = { healers[1], dps[1], dps[3] };
        }
        else if (dps.size() >= 6)
        {
            // No healers available - 3 DPS vs 3 DPS
            team1Info = { dps[0], dps[2], dps[4] };
            team2Info = { dps[1], dps[3], dps[5] };
        }
        else
        {
            return false; // Not enough players
        }
    }
    else // 2v2
    {
        // 2v2: Try 1 healer + 1 DPS per team
        if (healers.size() >= 2 && dps.size() >= 2)
        {
            team1Info = { healers[0], dps[0] };
            team2Info = { healers[1], dps[1] };
        }
        else if (healers.size() >= 1 && dps.size() >= 3)
        {
            team1Info = { healers[0], dps[0] };
            team2Info = { dps[1], dps[2] };
        }
        else if (dps.size() >= 4)
        {
            team1Info = { dps[0], dps[2] };
            team2Info = { dps[1], dps[3] };
        }
        else
        {
            return false;
        }
    }

    // Check MMR difference between teams
    uint32 team1MMR = 0, team2MMR = 0;
    for (auto* gi : team1Info)
        team1MMR += gi->ArenaMatchmakerRating;
    for (auto* gi : team2Info)
        team2MMR += gi->ArenaMatchmakerRating;
    team1MMR /= playersPerTeam;
    team2MMR /= playersPerTeam;

    if (_maxRatingDiff > 0)
    {
        int32 diff = static_cast<int32>(team1MMR) - static_cast<int32>(team2MMR);
        if (std::abs(diff) > static_cast<int32>(_maxRatingDiff))
            return false;
    }

    // Resolve players
    for (auto* gi : team1Info)
    {
        Player* p = ObjectAccessor::FindPlayer(*gi->Players.begin());
        if (!p) return false;
        team1.push_back(p);
    }
    for (auto* gi : team2Info)
    {
        Player* p = ObjectAccessor::FindPlayer(*gi->Players.begin());
        if (!p) return false;
        team2.push_back(p);
    }

    // Create arena instance
    PvPDifficultyEntry const* bracketEntry = sBattlegroundMgr->GetBattlegroundBracketByLevel(
        BATTLEGROUND_AA, team1[0]->GetLevel());
    if (!bracketEntry)
        return false;

    Battleground* arena = sBattlegroundMgr->CreateNewBattleground(
        BATTLEGROUND_AA, bracketEntry, arenaType, isRated);
    if (!arena)
    {
        LOG_ERROR("module", "SoloQueue: Failed to create arena instance");
        return false;
    }

    // Create temp arena teams
    uint8 teamType = (arenaType == ARENA_TYPE_2v2_SOLO) ? ARENA_TEAM_SOLO_2v2 : ARENA_TEAM_SOLO_3v3;

    ArenaTeam* at1 = new ArenaTeam();
    at1->CreateTempArenaTeam(team1, teamType, "SoloQ Team 1");
    sArenaTeamMgr->AddArenaTeam(at1);

    ArenaTeam* at2 = new ArenaTeam();
    at2->CreateTempArenaTeam(team2, teamType, "SoloQ Team 2");
    sArenaTeamMgr->AddArenaTeam(at2);

    arena->SetArenaTeamIdForTeam(TEAM_ALLIANCE, at1->GetId());
    arena->SetArenaTeamIdForTeam(TEAM_HORDE, at2->GetId());

    // Set opponent info on GroupQueueInfo and invite
    for (auto* gi : team1Info)
    {
        gi->OpponentsTeamRating = team2MMR;
        gi->OpponentsMatchmakerRating = team2MMR;
        queue->InviteGroupToBG(gi, arena, TEAM_ALLIANCE);
    }
    for (auto* gi : team2Info)
    {
        gi->OpponentsTeamRating = team1MMR;
        gi->OpponentsMatchmakerRating = team1MMR;
        queue->InviteGroupToBG(gi, arena, TEAM_HORDE);
    }

    arena->StartBattleground();

    LOG_INFO("module", "SoloQueue: {}v{} arena started (Team1 MMR: {}, Team2 MMR: {})",
        playersPerTeam, playersPerTeam, team1MMR, team2MMR);

    return true;
}

void SoloQueue::CountAsLoss(Player* player, uint8 arenaType)
{
    if (!player)
        return;

    uint8 slot = (arenaType == ARENA_TYPE_2v2_SOLO) ? ARENA_SLOT_SOLO_2v2 : ARENA_SLOT_SOLO_3v3;
    uint32 arenaTeamId = player->GetArenaTeamId(slot);

    ArenaTeam* team = sArenaTeamMgr->GetArenaTeamById(arenaTeamId);
    if (!team)
        return;

    int32 penalty = _leavePenalty;
    team->MemberLost(player, team->GetRating(), penalty);
    team->SaveToDB();
}
