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

#ifndef MOD_BG_QUEUE_ANNOUNCER_H
#define MOD_BG_QUEUE_ANNOUNCER_H

#include "Define.h"
#include "ObjectGuid.h"
#include <unordered_map>

class Battleground;
class Player;

enum BattlegroundBracketId : uint8;

class BgQueueAnnouncer
{
public:
    static BgQueueAnnouncer* instance();

    void LoadConfig();

    [[nodiscard]] bool IsEnabled() const;
    [[nodiscard]] bool IsPlayerOnly() const;
    [[nodiscard]] bool IsTimed() const;
    [[nodiscard]] uint32 GetTimer() const;
    [[nodiscard]] bool IsOnStartEnabled() const;

    // Spam protection
    bool CanAnnounce(Player* player, Battleground* bg, uint32 minLevel, uint32 queueTotal);

    // Timer management for timed announcements
    [[nodiscard]] int32 GetAnnouncementTimer(BattlegroundBracketId bracketId) const;
    void SetAnnouncementTimer(BattlegroundBracketId bracketId, int32 value);
    void UpdateAnnouncementTimer(BattlegroundBracketId bracketId, int32 diff);

private:
    BgQueueAnnouncer() = default;
    ~BgQueueAnnouncer() = default;

    void AddSpamTime(ObjectGuid guid);
    [[nodiscard]] uint32 GetSpamTime(ObjectGuid guid) const;
    [[nodiscard]] bool IsCorrectDelay(ObjectGuid guid) const;

    // Config values
    bool _enabled{false};
    bool _playerOnly{false};
    bool _timed{false};
    uint32 _timer{30000};
    uint32 _spamDelay{30};
    uint32 _limitMinLevel{0};
    uint32 _limitMinPlayers{3};
    bool _onStartEnable{true};

    // Spam protection data
    std::unordered_map<ObjectGuid, uint32> _spamProtect;

    // Timer data for timed announcements
    std::unordered_map<BattlegroundBracketId, int32> _queueAnnouncementTimer;
};

#define sBgQueueAnnouncer BgQueueAnnouncer::instance()

void AddSC_mod_bg_queue_announcer();

#endif // MOD_BG_QUEUE_ANNOUNCER_H
