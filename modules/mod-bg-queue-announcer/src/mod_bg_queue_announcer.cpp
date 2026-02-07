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

#include "mod_bg_queue_announcer.h"
#include "AllBattlegroundScript.h"
#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "BattlegroundQueue.h"
#include "BattlegroundUtils.h"
#include "Chat.h"
#include "Config.h"
#include "GameTime.h"
#include "Language.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "World.h"

BgQueueAnnouncer* BgQueueAnnouncer::instance()
{
    static BgQueueAnnouncer instance;
    return &instance;
}

void BgQueueAnnouncer::LoadConfig()
{
    _enabled = sConfigMgr->GetOption<bool>("BgQueueAnnouncer.Enable", false);
    _playerOnly = sConfigMgr->GetOption<bool>("BgQueueAnnouncer.PlayerOnly", false);
    _timed = sConfigMgr->GetOption<bool>("BgQueueAnnouncer.Timed", false);
    _timer = sConfigMgr->GetOption<uint32>("BgQueueAnnouncer.Timer", 30000);
    _spamDelay = sConfigMgr->GetOption<uint32>("BgQueueAnnouncer.SpamProtection.Delay", 30);
    _limitMinLevel = sConfigMgr->GetOption<uint32>("BgQueueAnnouncer.Limit.MinLevel", 0);
    _limitMinPlayers = sConfigMgr->GetOption<uint32>("BgQueueAnnouncer.Limit.MinPlayers", 3);
    _onStartEnable = sConfigMgr->GetOption<bool>("BgQueueAnnouncer.OnStart.Enable", true);
}

bool BgQueueAnnouncer::IsEnabled() const { return _enabled; }
bool BgQueueAnnouncer::IsPlayerOnly() const { return _playerOnly; }
bool BgQueueAnnouncer::IsTimed() const { return _timed; }
uint32 BgQueueAnnouncer::GetTimer() const { return _timer; }
bool BgQueueAnnouncer::IsOnStartEnabled() const { return _onStartEnable; }

void BgQueueAnnouncer::AddSpamTime(ObjectGuid guid)
{
    _spamProtect.insert_or_assign(guid, GameTime::GetGameTime().count());
}

uint32 BgQueueAnnouncer::GetSpamTime(ObjectGuid guid) const
{
    auto const& itr = _spamProtect.find(guid);
    if (itr != _spamProtect.end())
        return itr->second;
    return 0;
}

bool BgQueueAnnouncer::IsCorrectDelay(ObjectGuid guid) const
{
    return GameTime::GetGameTime().count() - GetSpamTime(guid) >= _spamDelay;
}

bool BgQueueAnnouncer::CanAnnounce(Player* player, Battleground* bg, uint32 minLevel, uint32 queueTotal)
{
    ObjectGuid guid = player->GetGUID();

    if (!IsCorrectDelay(guid))
        return false;

    if (bg && _limitMinLevel && minLevel >= _limitMinLevel)
    {
        auto bgTypeToLimit = minLevel == 80 ? BATTLEGROUND_RB : BATTLEGROUND_WS;
        if (bg->GetBgTypeID() == bgTypeToLimit && queueTotal < _limitMinPlayers)
            return false;
    }

    AddSpamTime(guid);
    return true;
}

int32 BgQueueAnnouncer::GetAnnouncementTimer(BattlegroundBracketId bracketId) const
{
    auto const& itr = _queueAnnouncementTimer.find(bracketId);
    if (itr != _queueAnnouncementTimer.end())
        return itr->second;
    return -1;
}

void BgQueueAnnouncer::SetAnnouncementTimer(BattlegroundBracketId bracketId, int32 value)
{
    _queueAnnouncementTimer[bracketId] = value;
}

void BgQueueAnnouncer::UpdateAnnouncementTimer(BattlegroundBracketId bracketId, int32 diff)
{
    auto itr = _queueAnnouncementTimer.find(bracketId);
    if (itr != _queueAnnouncementTimer.end() && itr->second >= 0)
        itr->second -= diff;
}

class BgQueueAnnouncerWorld : public WorldScript
{
public:
    BgQueueAnnouncerWorld() : WorldScript("BgQueueAnnouncerWorld") { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        sBgQueueAnnouncer->LoadConfig();
    }
};

class BgQueueAnnouncerBG : public AllBattlegroundScript
{
public:
    BgQueueAnnouncerBG() : AllBattlegroundScript("BgQueueAnnouncerBG") { }

    bool CanSendMessageBGQueue(BattlegroundQueue* queue, Player* leader, Battleground* bg, PvPDifficultyEntry const* bracketEntry) override
    {
        if (!sBgQueueAnnouncer->IsEnabled())
            return true; // Module disabled, let core handle it

        if (!bg || bg->isArena())
            return true; // Let core handle arenas

        BattlegroundBracketId bracketId = bracketEntry->GetBracketId();
        auto bgName = bg->GetName();
        uint32 MinPlayers = GetMinPlayersPerTeam(bg, bracketEntry);
        uint32 MaxPlayers = MinPlayers * 2;
        uint32 q_min_level = std::min(bracketEntry->minLevel, (uint32)80);
        uint32 q_max_level = std::min(bracketEntry->maxLevel, (uint32)80);
        uint32 qHorde = queue->GetPlayersCountInGroupsQueue(bracketId, BG_QUEUE_NORMAL_HORDE);
        uint32 qAlliance = queue->GetPlayersCountInGroupsQueue(bracketId, BG_QUEUE_NORMAL_ALLIANCE);
        auto qTotal = qHorde + qAlliance;

        if (sBgQueueAnnouncer->IsPlayerOnly())
        {
            // Custom format for player-only message (uses fmt style {})
            ChatHandler(leader->GetSession()).PSendSysMessage("|cff00ff00[BG Queue]|r {} |cffffff00(Lvl {}-{})|r - |cff0080ffAlliance:|r {} |cffff0000Horde:|r {} |cffaaaaaa[{}/{}]|r",
                bgName, q_min_level, q_max_level, qAlliance, qHorde, qTotal, MaxPlayers);
        }
        else
        {
            if (sBgQueueAnnouncer->IsTimed())
            {
                if (sBgQueueAnnouncer->GetAnnouncementTimer(bracketId) < 0)
                    sBgQueueAnnouncer->SetAnnouncementTimer(bracketId, sBgQueueAnnouncer->GetTimer());
            }
            else
            {
                if (!sBgQueueAnnouncer->CanAnnounce(leader, bg, q_min_level, qTotal))
                    return false; // Spam protection, block core but don't announce

                // Custom format for world announcement (uses fmt style {})
                ChatHandler(nullptr).SendWorldText(
                    "|cffff8000[BG Queue Announcer]|r |cff00ff00{}|r |cfffff000(Lvl {}-{})|r - |cff0080ffA:|r {} |cffff0000H:|r {} |cffaaaaaa[{}/{} players]|r",
                    bgName, q_min_level, q_max_level, qAlliance, qHorde, qTotal, MaxPlayers);
            }
        }

        return false; // We handled it, block core from announcing
    }

    void OnBattlegroundStart(Battleground* bg) override
    {
        if (!sBgQueueAnnouncer->IsEnabled() || !sBgQueueAnnouncer->IsOnStartEnabled())
            return;

        if (bg->isArena())
            return;

        // Custom format for BG start announcement (uses fmt style {})
        ChatHandler(nullptr).SendWorldText(
            "|cffff8000[BG Started]|r |cff00ff00{}|r |cffffff00(Lvl {}-{})|r |cffaaaaaahas begun!|r",
            bg->GetName(), std::min(bg->GetMinLevel(), (uint32)80), std::min(bg->GetMaxLevel(), (uint32)80));
    }

    void OnBattlegroundUpdate(Battleground* bg, uint32 /*diff*/) override
    {
        if (!sBgQueueAnnouncer->IsEnabled() || !sBgQueueAnnouncer->IsTimed())
            return;

        if (bg->isArena())
            return;

        // Handle timed announcements here if needed
    }
};

void AddSC_mod_bg_queue_announcer()
{
    new BgQueueAnnouncerWorld();
    new BgQueueAnnouncerBG();
}
