/*
 * mod-anticheat-pqr
 * Anti-PQR/Bot Detection Module for AzerothCore
 *
 * Detects automation tools like PQR by analyzing:
 * - Spell cast timing patterns
 * - Reaction times (interrupts, dispels)
 * - Rotation perfection metrics
 * - Movement patterns
 */

#ifndef MOD_ANTICHEAT_PQR_H
#define MOD_ANTICHEAT_PQR_H

#include "Player.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "Config.h"
#include "Log.h"
#include "Chat.h"
#include "World.h"
#include "DatabaseEnv.h"
#include "ObjectMgr.h"
#include "Warden.h"
#include "WardenPayloadMgr.h"
#include "WorldSession.h"

#include <unordered_map>
#include <deque>
#include <chrono>
#include <cmath>
#include <mutex>

// Forward declarations
class Player;
class Spell;

// ============================================================================
// Data Structures
// ============================================================================

// Represents a single spell cast event
struct SpellCastEvent
{
    uint32 spellId;
    uint32 timestamp;       // Server timestamp (getMSTime)
    uint32 targetGuid;
    bool wasSuccessful;
};

// Represents a reaction event (interrupt, dispel, etc.)
struct ReactionEvent
{
    uint32 triggerSpellId;  // The spell that triggered the reaction (enemy cast)
    uint32 reactionSpellId; // The spell used to react (interrupt/dispel)
    uint32 reactionTimeMs;  // Time between trigger and reaction
    uint32 timestamp;
};

// Warden-based detection data per player
struct WardenDetectionData
{
    bool wardenInitialized = false;
    bool payloadsRegistered = false;
    std::vector<uint16> registeredPayloadIds;  // IDs registered in WardenPayloadMgr
    uint32 lastWardenScanTime = 0;             // Last ForceChecks() timestamp
    uint32 wardenScanCount = 0;                // Total forced scans
    float wardenScore = 0.0f;                  // Warden detection score (0-100)
};

// Statistics for a player session
struct PlayerDetectionData
{
    ObjectGuid playerGuid;
    uint32 sessionStart;

    // Spell timing analysis
    std::deque<SpellCastEvent> recentCasts;
    std::unordered_map<uint32, std::deque<uint32>> spellIntervals; // spellId -> intervals between casts

    // Reaction time tracking
    std::deque<ReactionEvent> reactionEvents;
    uint32 lastEnemyCastStart;
    uint32 lastEnemyCastSpellId;

    // Pattern detection
    std::deque<uint32> rotationSequence;  // Last N spell IDs cast
    uint32 perfectRotationCount;
    uint32 totalRotationChecks;

    // Violation scores
    float timingScore;       // 0-100, higher = more suspicious
    float reactionScore;     // 0-100, higher = more suspicious
    float patternScore;      // 0-100, higher = more suspicious
    float overallScore;      // Combined score

    // Counters
    uint32 totalCasts;
    uint32 suspiciousTimings;
    uint32 inhumanReactions;
    uint32 perfectRotations;

    // Alerts
    uint32 alertLevel;       // 0=none, 1=low, 2=medium, 3=high
    uint32 lastAlertTime;

    // Warden integration
    WardenDetectionData wardenData;

    PlayerDetectionData()
    {
        sessionStart = 0;
        lastEnemyCastStart = 0;
        lastEnemyCastSpellId = 0;
        perfectRotationCount = 0;
        totalRotationChecks = 0;
        timingScore = 0.0f;
        reactionScore = 0.0f;
        patternScore = 0.0f;
        overallScore = 0.0f;
        totalCasts = 0;
        suspiciousTimings = 0;
        inhumanReactions = 0;
        perfectRotations = 0;
        alertLevel = 0;
        lastAlertTime = 0;
    }
};

// ============================================================================
// Main Manager Class
// ============================================================================

class AnticheatPqrMgr
{
public:
    static AnticheatPqrMgr* instance();

    // Initialization
    void LoadConfig();
    void Initialize();

    // Player tracking
    void OnPlayerLogin(Player* player);
    void OnPlayerLogout(Player* player);

    // Spell cast analysis
    void OnSpellCast(Player* player, Spell* spell);
    void OnSpellCastResult(Player* player, uint32 spellId, bool success);

    // Enemy cast tracking (for reaction detection)
    void OnEnemySpellCastStart(Player* player, Unit* caster, uint32 spellId);

    // Reaction detection
    void OnInterruptCast(Player* player, Unit* target, uint32 interruptSpellId);
    void OnDispelCast(Player* player, Unit* target, uint32 dispelSpellId);

    // Periodic analysis
    void OnPlayerUpdate(Player* player, uint32 diff);

    // Admin commands
    bool GetPlayerReport(Player* player, std::string& report);
    bool GetPlayerReport(ObjectGuid guid, std::string& report);
    void ResetPlayerData(Player* player);
    void ResetAllData();

    // Alert system
    void CheckAndAlert(Player* player, PlayerDetectionData& data);
    void LogViolation(Player* player, PlayerDetectionData& data, const std::string& reason);

    // Warden integration
    void InitWardenForPlayer(Player* player, PlayerDetectionData& data);
    void RegisterDynamicPayloads(Player* player, PlayerDetectionData& data);
    void TriggerWardenScan(Player* player, PlayerDetectionData& data);
    void CleanupWardenPayloads(Player* player, PlayerDetectionData& data);

    // Configuration getters
    bool IsEnabled() const { return _enabled; }
    bool IsWardenEnabled() const { return _wardenEnabled; }
    uint32 GetMinCastsForAnalysis() const { return _minCastsForAnalysis; }

private:
    AnticheatPqrMgr() = default;
    ~AnticheatPqrMgr() = default;

    // Analysis functions
    void AnalyzeSpellTiming(Player* player, PlayerDetectionData& data);
    void AnalyzeReactionTimes(PlayerDetectionData& data);
    void AnalyzeRotationPattern(PlayerDetectionData& data);
    void CalculateOverallScore(PlayerDetectionData& data);

    // Utility functions
    float CalculateStandardDeviation(const std::deque<uint32>& values);
    bool IsRotationSpell(uint32 spellId);
    bool IsInterruptSpell(uint32 spellId);
    bool IsDispelSpell(uint32 spellId);

    // Player data storage
    std::unordered_map<ObjectGuid, PlayerDetectionData> _playerData;
    std::mutex _dataMutex;

    // Configuration
    bool _enabled;
    bool _logToDatabase;
    bool _alertGMs;
    bool _autoBan;

    // Thresholds
    uint32 _minCastsForAnalysis;
    uint32 _maxHistorySize;
    uint32 _rotationWindowSize;

    // Timing thresholds (in milliseconds)
    float _suspiciousTimingVariance;    // Below this std dev is suspicious
    uint32 _minReactionTimeMs;          // Reactions faster than this are suspicious
    uint32 _humanReactionMinMs;         // Minimum human reaction time

    // Score thresholds
    float _alertThresholdLow;
    float _alertThresholdMedium;
    float _alertThresholdHigh;
    float _autoBanThreshold;

    // Cooldowns
    uint32 _alertCooldownMs;
    uint32 _analysisIntervalMs;

    // Warden integration config
    bool _wardenEnabled;
    uint32 _wardenScanIntervalMs;
    float _wardenScoreWeight;
    bool _wardenForceOnSuspicion;

    // Last analysis timestamps
    std::unordered_map<ObjectGuid, uint32> _lastAnalysisTime;
};

#define sAnticheatPqrMgr AnticheatPqrMgr::instance()

// ============================================================================
// Known Spell Lists
// ============================================================================

namespace AnticheatPqrSpells
{
    // Interrupt spells by class
    constexpr uint32 SPELL_KICK = 1766;              // Rogue
    constexpr uint32 SPELL_PUMMEL = 6552;            // Warrior
    constexpr uint32 SPELL_SHIELD_BASH = 72;         // Warrior
    constexpr uint32 SPELL_COUNTERSPELL = 2139;      // Mage
    constexpr uint32 SPELL_EARTH_SHOCK = 8042;       // Shaman (with talent)
    constexpr uint32 SPELL_WIND_SHEAR = 57994;       // Shaman
    constexpr uint32 SPELL_MIND_FREEZE = 47528;      // Death Knight
    constexpr uint32 SPELL_STRANGULATE = 47476;      // Death Knight
    constexpr uint32 SPELL_FERAL_CHARGE_CAT = 49376; // Druid
    constexpr uint32 SPELL_FERAL_CHARGE_BEAR = 16979;// Druid
    constexpr uint32 SPELL_SILENCE = 15487;          // Priest
    constexpr uint32 SPELL_SPELL_LOCK = 19647;       // Warlock (Felhunter)
    constexpr uint32 SPELL_ARCANE_TORRENT_MANA = 28730;  // Blood Elf
    constexpr uint32 SPELL_ARCANE_TORRENT_RUNIC = 50613; // Blood Elf DK
    constexpr uint32 SPELL_ARCANE_TORRENT_ENERGY = 25046;// Blood Elf Rogue

    // Dispel spells
    constexpr uint32 SPELL_DISPEL_MAGIC = 527;       // Priest
    constexpr uint32 SPELL_MASS_DISPEL = 32375;      // Priest
    constexpr uint32 SPELL_CLEANSE = 4987;           // Paladin
    constexpr uint32 SPELL_PURGE = 370;              // Shaman
    constexpr uint32 SPELL_REMOVE_CURSE = 475;       // Mage
    constexpr uint32 SPELL_REMOVE_CURSE_DRUID = 2782;// Druid
    constexpr uint32 SPELL_ABOLISH_DISEASE = 552;    // Priest
    constexpr uint32 SPELL_ABOLISH_POISON = 2893;    // Druid
    constexpr uint32 SPELL_DEVOUR_MAGIC = 19505;     // Warlock (Felhunter)

    // Check functions
    inline bool IsInterruptSpell(uint32 spellId)
    {
        switch (spellId)
        {
            case SPELL_KICK:
            case SPELL_PUMMEL:
            case SPELL_SHIELD_BASH:
            case SPELL_COUNTERSPELL:
            case SPELL_EARTH_SHOCK:
            case SPELL_WIND_SHEAR:
            case SPELL_MIND_FREEZE:
            case SPELL_STRANGULATE:
            case SPELL_FERAL_CHARGE_CAT:
            case SPELL_FERAL_CHARGE_BEAR:
            case SPELL_SILENCE:
            case SPELL_SPELL_LOCK:
            case SPELL_ARCANE_TORRENT_MANA:
            case SPELL_ARCANE_TORRENT_RUNIC:
            case SPELL_ARCANE_TORRENT_ENERGY:
                return true;
            default:
                return false;
        }
    }

    inline bool IsDispelSpell(uint32 spellId)
    {
        switch (spellId)
        {
            case SPELL_DISPEL_MAGIC:
            case SPELL_MASS_DISPEL:
            case SPELL_CLEANSE:
            case SPELL_PURGE:
            case SPELL_REMOVE_CURSE:
            case SPELL_REMOVE_CURSE_DRUID:
            case SPELL_ABOLISH_DISEASE:
            case SPELL_ABOLISH_POISON:
            case SPELL_DEVOUR_MAGIC:
                return true;
            default:
                return false;
        }
    }
}

#endif // MOD_ANTICHEAT_PQR_H
