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

#ifndef MOD_SOLOQUEUE_H
#define MOD_SOLOQUEUE_H

#include "Define.h"
#include "ObjectGuid.h"
#include <vector>
#include <unordered_map>

class Player;
class Battleground;
class BattlegroundQueue;
struct GroupQueueInfo;
enum BattlegroundBracketId : uint8;

// Custom ArenaType IDs (must not conflict with core: 2, 3, 5)
#define ARENA_TYPE_2v2_SOLO         6
#define ARENA_TYPE_3v3_SOLO         4

// Custom Arena Team type IDs
#define ARENA_TEAM_SOLO_2v2         42
#define ARENA_TEAM_SOLO_3v3         43

// Custom Arena Slots (core uses 0=2v2, 1=3v3, 2=5v5)
#define ARENA_SLOT_SOLO_2v2         3
#define ARENA_SLOT_SOLO_3v3         4

// Custom BattlegroundQueueTypeId (core uses up to 10)
#define BATTLEGROUND_QUEUE_2v2_SOLO 11
#define BATTLEGROUND_QUEUE_3v3_SOLO 12

// NPC Entry for Solo Queue Master
#define NPC_SOLOQUEUE_MASTER        200000

// Deserter debuff spell
#define SPELL_DESERTER              26013

// Talent category for role-based matchmaking
enum SoloQueueTalentCat : uint8
{
    SOLOQ_MELEE  = 0,
    SOLOQ_RANGE  = 1,
    SOLOQ_HEALER = 2
};

class SoloQueue
{
public:
    static SoloQueue* instance();

    void LoadConfig();

    // Config getters
    [[nodiscard]] bool IsEnabled2v2() const { return _enable2v2; }
    [[nodiscard]] bool IsEnabled3v3() const { return _enable3v3; }
    [[nodiscard]] uint32 GetMinLevel() const { return _minLevel; }
    [[nodiscard]] uint32 GetCost() const { return _cost; }
    [[nodiscard]] bool FilterTalents() const { return _filterTalents; }
    [[nodiscard]] uint32 GetMaxRatingDiff() const { return _maxRatingDiff; }
    [[nodiscard]] bool CastDeserterOnLeave() const { return _deserterOnLeave; }
    [[nodiscard]] bool CastDeserterOnAfk() const { return _deserterOnAfk; }
    [[nodiscard]] bool StopGameIncomplete() const { return _stopIncomplete; }
    [[nodiscard]] int32 GetLeavePenalty() const { return _leavePenalty; }
    [[nodiscard]] int32 GetLeaveBeforeStartPenalty() const { return _leaveBeforeStartPenalty; }
    [[nodiscard]] float GetArenaPointsMulti() const { return _arenaPointsMulti; }
    [[nodiscard]] bool ShowLoginMessage() const { return _showLoginMsg; }
    [[nodiscard]] bool CheckEquipAndTalents() const { return _checkEquip; }

    // Talent/Role classification
    SoloQueueTalentCat GetTalentCategory(Player* player);

    // Matchmaking
    bool CheckSoloArena(BattlegroundQueue* queue, BattlegroundBracketId bracketId,
                        uint8 arenaType, bool isRated);

    // Rating penalty for leaving
    void CountAsLoss(Player* player, uint8 arenaType);

    // Equipment check
    bool ArenaCheckFullEquipAndTalents(Player* player);

private:
    SoloQueue() = default;
    ~SoloQueue() = default;

    // Config values
    bool _enable2v2{true};
    bool _enable3v3{true};
    uint32 _minLevel{80};
    uint32 _cost{0};
    bool _filterTalents{true};
    uint32 _maxRatingDiff{150};
    bool _deserterOnLeave{true};
    bool _deserterOnAfk{true};
    bool _stopIncomplete{true};
    int32 _leavePenalty{24};
    int32 _leaveBeforeStartPenalty{50};
    float _arenaPointsMulti{0.8f};
    bool _showLoginMsg{true};
    bool _checkEquip{false};
};

#define sSoloQueue SoloQueue::instance()

#endif // MOD_SOLOQUEUE_H
