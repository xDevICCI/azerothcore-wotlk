/*
 * mod-anticheat-pqr
 * Anti-PQR/Bot Detection Module for AzerothCore
 */

#include "mod_anticheat_pqr.h"
#include "ScriptMgr.h"
#include "Player.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "Chat.h"
#include "Config.h"
#include "Log.h"
#include "World.h"
#include "WorldSession.h"
#include "GameTime.h"

// ============================================================================
// Singleton Implementation
// ============================================================================

AnticheatPqrMgr* AnticheatPqrMgr::instance()
{
    static AnticheatPqrMgr instance;
    return &instance;
}

// ============================================================================
// Initialization
// ============================================================================

void AnticheatPqrMgr::LoadConfig()
{
    _enabled = sConfigMgr->GetOption<bool>("AnticheatPqr.Enable", true);
    _logToDatabase = sConfigMgr->GetOption<bool>("AnticheatPqr.LogToDatabase", true);
    _alertGMs = sConfigMgr->GetOption<bool>("AnticheatPqr.AlertGMs", true);
    _autoBan = sConfigMgr->GetOption<bool>("AnticheatPqr.AutoBan", false);

    _minCastsForAnalysis = sConfigMgr->GetOption<uint32>("AnticheatPqr.MinCastsForAnalysis", 20);
    _maxHistorySize = sConfigMgr->GetOption<uint32>("AnticheatPqr.MaxHistorySize", 100);
    _rotationWindowSize = sConfigMgr->GetOption<uint32>("AnticheatPqr.RotationWindowSize", 10);

    _suspiciousTimingVariance = sConfigMgr->GetOption<float>("AnticheatPqr.SuspiciousTimingVariance", 15.0f);
    _minReactionTimeMs = sConfigMgr->GetOption<uint32>("AnticheatPqr.MinReactionTimeMs", 80);
    _humanReactionMinMs = sConfigMgr->GetOption<uint32>("AnticheatPqr.HumanReactionMinMs", 150);

    _alertThresholdLow = sConfigMgr->GetOption<float>("AnticheatPqr.AlertThresholdLow", 40.0f);
    _alertThresholdMedium = sConfigMgr->GetOption<float>("AnticheatPqr.AlertThresholdMedium", 60.0f);
    _alertThresholdHigh = sConfigMgr->GetOption<float>("AnticheatPqr.AlertThresholdHigh", 80.0f);
    _autoBanThreshold = sConfigMgr->GetOption<float>("AnticheatPqr.AutoBanThreshold", 95.0f);

    _alertCooldownMs = sConfigMgr->GetOption<uint32>("AnticheatPqr.AlertCooldownMs", 60000);
    _analysisIntervalMs = sConfigMgr->GetOption<uint32>("AnticheatPqr.AnalysisIntervalMs", 5000);

    // Warden integration config
    _wardenEnabled = sConfigMgr->GetOption<bool>("AnticheatPqr.Warden.Enable", true);
    _wardenScanIntervalMs = sConfigMgr->GetOption<uint32>("AnticheatPqr.Warden.ScanIntervalMs", 30000);
    _wardenScoreWeight = sConfigMgr->GetOption<float>("AnticheatPqr.Warden.ScoreWeight", 0.30f);
    _wardenForceOnSuspicion = sConfigMgr->GetOption<bool>("AnticheatPqr.Warden.ForceOnSuspicion", true);

    LOG_INFO("module", ">> AnticheatPqr: Configuration loaded (Enabled: {}, Warden: {})",
        _enabled ? "Yes" : "No", _wardenEnabled ? "Yes" : "No");
}

void AnticheatPqrMgr::Initialize()
{
    LoadConfig();
    LOG_INFO("module", ">> AnticheatPqr: Module initialized");
}

// ============================================================================
// Player Tracking
// ============================================================================

void AnticheatPqrMgr::OnPlayerLogin(Player* player)
{
    if (!_enabled || !player)
        return;

    std::lock_guard<std::mutex> lock(_dataMutex);

    PlayerDetectionData data;
    data.playerGuid = player->GetGUID();
    data.sessionStart = GameTime::GetGameTimeMS();
    _playerData[player->GetGUID()] = data;

    // Initialize Warden integration for this player
    InitWardenForPlayer(player, _playerData[player->GetGUID()]);

    LOG_DEBUG("module", "AnticheatPqr: Started tracking player {} ({})",
        player->GetName(), player->GetGUID().ToString());
}

void AnticheatPqrMgr::OnPlayerLogout(Player* player)
{
    if (!_enabled || !player)
        return;

    std::lock_guard<std::mutex> lock(_dataMutex);

    auto it = _playerData.find(player->GetGUID());
    if (it != _playerData.end())
    {
        // Cleanup Warden payloads before erasing data
        CleanupWardenPayloads(player, it->second);

        // Log final statistics if suspicious
        if (it->second.overallScore >= _alertThresholdLow)
        {
            LogViolation(player, it->second, "Session ended with elevated score");
        }
        _playerData.erase(it);
    }

    _lastAnalysisTime.erase(player->GetGUID());

    LOG_DEBUG("module", "AnticheatPqr: Stopped tracking player {} ({})",
        player->GetName(), player->GetGUID().ToString());
}

// ============================================================================
// Spell Cast Analysis
// ============================================================================

void AnticheatPqrMgr::OnSpellCast(Player* player, Spell* spell)
{
    if (!_enabled || !player || !spell)
        return;

    SpellInfo const* spellInfo = spell->GetSpellInfo();
    if (!spellInfo)
        return;

    // Ignore passive, triggered, and channeled spells for timing analysis
    if (spellInfo->IsPassive() || spell->IsTriggered())
        return;

    // Ignore non-combat spells (buffs, mounts, professions, etc.)
    if (!spellInfo->HasAttribute(SPELL_ATTR0_IS_ABILITY) &&
        !spellInfo->HasAttribute(SPELL_ATTR0_TRACK_TARGET_IN_CAST))
        return;

    std::lock_guard<std::mutex> lock(_dataMutex);

    auto it = _playerData.find(player->GetGUID());
    if (it == _playerData.end())
        return;

    PlayerDetectionData& data = it->second;
    uint32 currentTime = GameTime::GetGameTimeMS();
    uint32 spellId = spellInfo->Id;

    // Record cast event
    SpellCastEvent event;
    event.spellId = spellId;
    event.timestamp = currentTime;
    event.targetGuid = spell->m_targets.GetUnitTargetGUID().GetCounter();
    event.wasSuccessful = true; // Will be updated in OnSpellCastResult

    data.recentCasts.push_back(event);
    data.totalCasts++;

    // Maintain history size
    while (data.recentCasts.size() > _maxHistorySize)
        data.recentCasts.pop_front();

    // Calculate interval from previous cast of same spell
    if (data.recentCasts.size() >= 2)
    {
        for (auto rit = data.recentCasts.rbegin() + 1; rit != data.recentCasts.rend(); ++rit)
        {
            if (rit->spellId == spellId)
            {
                uint32 interval = currentTime - rit->timestamp;
                data.spellIntervals[spellId].push_back(interval);

                // Limit interval history per spell
                while (data.spellIntervals[spellId].size() > 20)
                    data.spellIntervals[spellId].pop_front();

                break;
            }
        }
    }

    // Track rotation sequence
    data.rotationSequence.push_back(spellId);
    while (data.rotationSequence.size() > _rotationWindowSize * 3)
        data.rotationSequence.pop_front();

    // Check for interrupt usage (reaction tracking)
    if (AnticheatPqrSpells::IsInterruptSpell(spellId))
    {
        OnInterruptCast(player, spell->m_targets.GetUnitTarget(), spellId);
    }
    else if (AnticheatPqrSpells::IsDispelSpell(spellId))
    {
        OnDispelCast(player, spell->m_targets.GetUnitTarget(), spellId);
    }
}

void AnticheatPqrMgr::OnSpellCastResult(Player* player, uint32 spellId, bool success)
{
    if (!_enabled || !player)
        return;

    std::lock_guard<std::mutex> lock(_dataMutex);

    auto it = _playerData.find(player->GetGUID());
    if (it == _playerData.end())
        return;

    // Update the most recent cast with this spell ID
    for (auto rit = it->second.recentCasts.rbegin(); rit != it->second.recentCasts.rend(); ++rit)
    {
        if (rit->spellId == spellId)
        {
            rit->wasSuccessful = success;
            break;
        }
    }
}

// ============================================================================
// Enemy Cast Tracking (for reaction detection)
// ============================================================================

void AnticheatPqrMgr::OnEnemySpellCastStart(Player* player, Unit* caster, uint32 spellId)
{
    if (!_enabled || !player || !caster)
        return;

    // Only track if enemy is casting at the player or near them
    if (!player->IsHostileTo(caster))
        return;

    std::lock_guard<std::mutex> lock(_dataMutex);

    auto it = _playerData.find(player->GetGUID());
    if (it == _playerData.end())
        return;

    it->second.lastEnemyCastStart = GameTime::GetGameTimeMS();
    it->second.lastEnemyCastSpellId = spellId;
}

// ============================================================================
// Reaction Detection
// ============================================================================

void AnticheatPqrMgr::OnInterruptCast(Player* player, Unit* target, uint32 interruptSpellId)
{
    if (!player || !target)
        return;

    // Already locked by caller

    auto it = _playerData.find(player->GetGUID());
    if (it == _playerData.end())
        return;

    PlayerDetectionData& data = it->second;
    uint32 currentTime = GameTime::GetGameTimeMS();

    // Check if there was a recent enemy cast
    if (data.lastEnemyCastStart > 0)
    {
        uint32 reactionTime = currentTime - data.lastEnemyCastStart;

        ReactionEvent event;
        event.triggerSpellId = data.lastEnemyCastSpellId;
        event.reactionSpellId = interruptSpellId;
        event.reactionTimeMs = reactionTime;
        event.timestamp = currentTime;

        data.reactionEvents.push_back(event);

        // Maintain history size
        while (data.reactionEvents.size() > 50)
            data.reactionEvents.pop_front();

        // Check for inhuman reaction time
        if (reactionTime < _minReactionTimeMs)
        {
            data.inhumanReactions++;
            LOG_DEBUG("module", "AnticheatPqr: Player {} had inhuman reaction time: {}ms (interrupt)",
                player->GetName(), reactionTime);
        }

        // Reset enemy cast tracking
        data.lastEnemyCastStart = 0;
        data.lastEnemyCastSpellId = 0;
    }
}

void AnticheatPqrMgr::OnDispelCast(Player* player, Unit* target, uint32 dispelSpellId)
{
    if (!player || !target)
        return;

    // Similar to interrupt tracking but for dispels
    // Dispels on debuffs that just appeared are suspicious if reaction is too fast

    auto it = _playerData.find(player->GetGUID());
    if (it == _playerData.end())
        return;

    // For dispels, we'd need to track when debuffs are applied to properly measure reaction
    // This is a simplified version - full implementation would hook into aura application
}

// ============================================================================
// Periodic Analysis
// ============================================================================

void AnticheatPqrMgr::OnPlayerUpdate(Player* player, uint32 diff)
{
    if (!_enabled || !player)
        return;

    uint32 currentTime = GameTime::GetGameTimeMS();
    ObjectGuid guid = player->GetGUID();

    // Check if enough time has passed since last analysis
    auto lastTime = _lastAnalysisTime.find(guid);
    if (lastTime != _lastAnalysisTime.end())
    {
        if (currentTime - lastTime->second < _analysisIntervalMs)
            return;
    }

    std::lock_guard<std::mutex> lock(_dataMutex);

    auto it = _playerData.find(guid);
    if (it == _playerData.end())
        return;

    PlayerDetectionData& data = it->second;

    // Lazy Warden initialization (Warden may not be ready at login time)
    if (_wardenEnabled && !data.wardenData.wardenInitialized)
    {
        InitWardenForPlayer(player, data);
    }

    // Only analyze if we have enough data
    if (data.totalCasts < _minCastsForAnalysis)
        return;

    _lastAnalysisTime[guid] = currentTime;

    // Run all analysis
    AnalyzeSpellTiming(player, data);
    AnalyzeReactionTimes(data);
    AnalyzeRotationPattern(data);
    CalculateOverallScore(data);

    // Check for alerts
    CheckAndAlert(player, data);
}

// ============================================================================
// Analysis Functions
// ============================================================================

void AnticheatPqrMgr::AnalyzeSpellTiming(Player* player, PlayerDetectionData& data)
{
    if (data.spellIntervals.empty())
        return;

    uint32 suspiciousSpells = 0;
    uint32 totalSpellsAnalyzed = 0;

    for (auto& pair : data.spellIntervals)
    {
        const std::deque<uint32>& intervals = pair.second;

        if (intervals.size() < 5)
            continue;

        totalSpellsAnalyzed++;

        // Calculate standard deviation of intervals
        float stdDev = CalculateStandardDeviation(intervals);

        // Bots typically have very low variance in timing
        // Human players have natural variance of 50-200ms typically
        if (stdDev < _suspiciousTimingVariance)
        {
            suspiciousSpells++;
            data.suspiciousTimings++;
        }
    }

    // Calculate timing score
    if (totalSpellsAnalyzed > 0)
    {
        data.timingScore = (static_cast<float>(suspiciousSpells) / totalSpellsAnalyzed) * 100.0f;
    }
}

void AnticheatPqrMgr::AnalyzeReactionTimes(PlayerDetectionData& data)
{
    if (data.reactionEvents.empty())
        return;

    uint32 inhumanCount = 0;
    uint32 suspiciousCount = 0;

    for (const auto& event : data.reactionEvents)
    {
        if (event.reactionTimeMs < _minReactionTimeMs)
        {
            inhumanCount++;
        }
        else if (event.reactionTimeMs < _humanReactionMinMs)
        {
            suspiciousCount++;
        }
    }

    // Calculate reaction score
    // Inhuman reactions are weighted heavily
    float inhumanRatio = static_cast<float>(inhumanCount) / data.reactionEvents.size();
    float suspiciousRatio = static_cast<float>(suspiciousCount) / data.reactionEvents.size();

    data.reactionScore = (inhumanRatio * 80.0f) + (suspiciousRatio * 20.0f);
    data.reactionScore = std::min(data.reactionScore, 100.0f);
}

void AnticheatPqrMgr::AnalyzeRotationPattern(PlayerDetectionData& data)
{
    if (data.rotationSequence.size() < _rotationWindowSize * 2)
        return;

    // Look for repeating patterns in the rotation
    // Bots often follow exact rotation sequences

    data.totalRotationChecks++;

    // Extract windows and compare
    std::vector<uint32> pattern1, pattern2;

    size_t windowStart = data.rotationSequence.size() - _rotationWindowSize * 2;
    for (size_t i = 0; i < _rotationWindowSize; i++)
    {
        pattern1.push_back(data.rotationSequence[windowStart + i]);
        pattern2.push_back(data.rotationSequence[windowStart + _rotationWindowSize + i]);
    }

    // Check if patterns match
    bool exactMatch = (pattern1 == pattern2);

    if (exactMatch)
    {
        data.perfectRotationCount++;
    }

    // Calculate pattern score
    if (data.totalRotationChecks > 5)
    {
        float perfectRatio = static_cast<float>(data.perfectRotationCount) / data.totalRotationChecks;

        // Some repetition is normal, but > 50% exact matches is suspicious
        if (perfectRatio > 0.5f)
        {
            data.patternScore = (perfectRatio - 0.5f) * 200.0f; // Scale 0.5-1.0 to 0-100
            data.patternScore = std::min(data.patternScore, 100.0f);
        }
        else
        {
            data.patternScore = 0.0f;
        }
    }
}

void AnticheatPqrMgr::CalculateOverallScore(PlayerDetectionData& data)
{
    // Weighted combination of all scores
    // When Warden is enabled and initialized, rebalance weights to include Warden score
    if (_wardenEnabled && data.wardenData.wardenInitialized)
    {
        // Warden-enhanced scoring: behavioral (70%) + warden (30%)
        data.overallScore =
            (data.reactionScore * 0.35f) +   // 35% reaction time
            (data.timingScore * 0.25f) +      // 25% timing variance
            (data.patternScore * 0.10f) +     // 10% rotation patterns
            (data.wardenData.wardenScore * _wardenScoreWeight); // 30% warden detection
    }
    else
    {
        // Original behavioral-only scoring
        data.overallScore =
            (data.reactionScore * 0.45f) +    // 45% reaction time
            (data.timingScore * 0.35f) +      // 35% timing variance
            (data.patternScore * 0.20f);      // 20% rotation patterns
    }

    data.overallScore = std::min(data.overallScore, 100.0f);
}

// ============================================================================
// Alert System
// ============================================================================

void AnticheatPqrMgr::CheckAndAlert(Player* player, PlayerDetectionData& data)
{
    uint32 currentTime = GameTime::GetGameTimeMS();

    // Force Warden scan if behavioral score exceeds threshold
    if (_wardenForceOnSuspicion && data.overallScore >= _alertThresholdLow)
    {
        TriggerWardenScan(player, data);
    }

    // Check cooldown
    if (data.lastAlertTime > 0 && currentTime - data.lastAlertTime < _alertCooldownMs)
        return;

    uint32 newAlertLevel = 0;

    if (data.overallScore >= _alertThresholdHigh)
        newAlertLevel = 3;
    else if (data.overallScore >= _alertThresholdMedium)
        newAlertLevel = 2;
    else if (data.overallScore >= _alertThresholdLow)
        newAlertLevel = 1;

    // Only alert if level increased
    if (newAlertLevel > data.alertLevel)
    {
        data.alertLevel = newAlertLevel;
        data.lastAlertTime = currentTime;

        std::string alertMessage;
        switch (newAlertLevel)
        {
            case 1:
                alertMessage = "LOW suspicion";
                break;
            case 2:
                alertMessage = "MEDIUM suspicion";
                break;
            case 3:
                alertMessage = "HIGH suspicion";
                break;
        }

        // Log to database
        if (_logToDatabase)
        {
            LogViolation(player, data, alertMessage);
        }

        // Alert GMs
        if (_alertGMs && newAlertLevel >= 2)
        {
            std::string gmAlert = Acore::StringFormat(
                "|cFFFF0000[AnticheatPqr]|r Player |cFF00FF00{}|r has {} (Score: {:.1f}) "
                "[Timing: {:.1f}, Reaction: {:.1f}, Pattern: {:.1f}]",
                player->GetName(), alertMessage, data.overallScore,
                data.timingScore, data.reactionScore, data.patternScore);

            sWorld->SendGMText(LANG_GM_BROADCAST, gmAlert.c_str());
        }

        // Auto-ban if enabled and threshold reached
        if (_autoBan && data.overallScore >= _autoBanThreshold)
        {
            LOG_WARN("module", "AnticheatPqr: Player {} would be auto-banned (Score: {:.1f}) - AutoBan is {}",
                player->GetName(), data.overallScore, _autoBan ? "ENABLED" : "DISABLED");

            // Implement actual ban here if desired
            // sWorld->BanAccount(BAN_ACCOUNT, player->GetSession()->GetAccountId(), ...);
        }
    }
}

void AnticheatPqrMgr::LogViolation(Player* player, PlayerDetectionData& data, const std::string& reason)
{
    if (!_logToDatabase)
        return;

    CharacterDatabase.Execute(
        "INSERT INTO anticheat_pqr_logs (guid, account_id, player_name, overall_score, "
        "timing_score, reaction_score, pattern_score, total_casts, suspicious_timings, "
        "inhuman_reactions, alert_level, reason, timestamp) "
        "VALUES ({}, {}, '{}', {:.2f}, {:.2f}, {:.2f}, {:.2f}, {}, {}, {}, {}, '{}', NOW())",
        player->GetGUID().GetCounter(),
        player->GetSession()->GetAccountId(),
        player->GetName(),
        data.overallScore,
        data.timingScore,
        data.reactionScore,
        data.patternScore,
        data.totalCasts,
        data.suspiciousTimings,
        data.inhumanReactions,
        data.alertLevel,
        reason);
}

// ============================================================================
// Admin Commands
// ============================================================================

bool AnticheatPqrMgr::GetPlayerReport(Player* player, std::string& report)
{
    if (!player)
        return false;

    return GetPlayerReport(player->GetGUID(), report);
}

bool AnticheatPqrMgr::GetPlayerReport(ObjectGuid guid, std::string& report)
{
    std::lock_guard<std::mutex> lock(_dataMutex);

    auto it = _playerData.find(guid);
    if (it == _playerData.end())
    {
        report = "No data available for this player.";
        return false;
    }

    const PlayerDetectionData& data = it->second;

    report = Acore::StringFormat(
        "=== AnticheatPqr Report ===\n"
        "Overall Score: {:.1f}/100\n"
        "Alert Level: {}\n"
        "---\n"
        "Timing Score: {:.1f}/100\n"
        "  - Total Casts: {}\n"
        "  - Suspicious Timings: {}\n"
        "---\n"
        "Reaction Score: {:.1f}/100\n"
        "  - Reaction Events: {}\n"
        "  - Inhuman Reactions: {}\n"
        "---\n"
        "Pattern Score: {:.1f}/100\n"
        "  - Perfect Rotations: {}/{}\n"
        "---\n"
        "Warden Score: {:.1f}/100\n"
        "  - Warden Initialized: {}\n"
        "  - Payloads Registered: {}\n"
        "  - Forced Scans: {}\n",
        data.overallScore,
        data.alertLevel,
        data.timingScore,
        data.totalCasts,
        data.suspiciousTimings,
        data.reactionScore,
        data.reactionEvents.size(),
        data.inhumanReactions,
        data.patternScore,
        data.perfectRotationCount,
        data.totalRotationChecks,
        data.wardenData.wardenScore,
        data.wardenData.wardenInitialized ? "Yes" : "No",
        data.wardenData.registeredPayloadIds.size(),
        data.wardenData.wardenScanCount);

    return true;
}

void AnticheatPqrMgr::ResetPlayerData(Player* player)
{
    if (!player)
        return;

    std::lock_guard<std::mutex> lock(_dataMutex);
    _playerData.erase(player->GetGUID());
    _lastAnalysisTime.erase(player->GetGUID());

    // Re-initialize
    PlayerDetectionData data;
    data.playerGuid = player->GetGUID();
    data.sessionStart = GameTime::GetGameTimeMS();
    _playerData[player->GetGUID()] = data;
}

void AnticheatPqrMgr::ResetAllData()
{
    std::lock_guard<std::mutex> lock(_dataMutex);
    _playerData.clear();
    _lastAnalysisTime.clear();
}

// ============================================================================
// Utility Functions
// ============================================================================

float AnticheatPqrMgr::CalculateStandardDeviation(const std::deque<uint32>& values)
{
    if (values.size() < 2)
        return 0.0f;

    // Calculate mean
    float sum = 0.0f;
    for (uint32 val : values)
        sum += static_cast<float>(val);

    float mean = sum / values.size();

    // Calculate variance
    float variance = 0.0f;
    for (uint32 val : values)
    {
        float diff = static_cast<float>(val) - mean;
        variance += diff * diff;
    }
    variance /= values.size();

    return std::sqrt(variance);
}

bool AnticheatPqrMgr::IsInterruptSpell(uint32 spellId)
{
    return AnticheatPqrSpells::IsInterruptSpell(spellId);
}

bool AnticheatPqrMgr::IsDispelSpell(uint32 spellId)
{
    return AnticheatPqrSpells::IsDispelSpell(spellId);
}

// ============================================================================
// Warden Integration
// ============================================================================

void AnticheatPqrMgr::InitWardenForPlayer(Player* player, PlayerDetectionData& data)
{
    if (!_wardenEnabled || !_enabled)
        return;

    WorldSession* session = player->GetSession();
    if (!session)
        return;

    Warden* warden = session->GetWarden();
    if (!warden || !warden->IsInitialized())
    {
        LOG_DEBUG("module", "AnticheatPqr: Warden not initialized for player {} - will retry on update",
            player->GetName());
        return;
    }

    data.wardenData.wardenInitialized = true;
    RegisterDynamicPayloads(player, data);

    LOG_DEBUG("module", "AnticheatPqr: Warden initialized for player {} ({} payloads registered)",
        player->GetName(), data.wardenData.registeredPayloadIds.size());
}

void AnticheatPqrMgr::RegisterDynamicPayloads(Player* player, PlayerDetectionData& data)
{
    if (data.wardenData.payloadsRegistered)
        return;

    Warden* warden = player->GetSession()->GetWarden();
    if (!warden || !warden->IsInitialized())
        return;

    WardenPayloadMgr* payloadMgr = warden->GetPayloadMgr();
    if (!payloadMgr)
        return;

    // Dynamic Lua payloads to detect PQR internal variables
    // These complement the static LUA_EVAL_CHECK entries in warden_checks
    struct PayloadDef
    {
        const char* lua;
        const char* desc;
    };

    static const PayloadDef payloads[] =
    {
        { "return not not PQR_CurrentRotation",                       "PQR current rotation table" },
        { "local o=_G['PQR_SpellCast'] return not not o",            "PQR spell cast hook" },
        { "return not not PQR_RotationSelection",                     "PQR rotation selection" },
        { "return not not PQR_CombatTracking",                        "PQR combat tracking" },
        { "return type(RunRotation)=='function'",                     "PQR RunRotation function" },
        { "return not not PQR_InterruptDelay",                        "PQR interrupt delay var" },
    };

    for (const auto& p : payloads)
    {
        uint16 id = payloadMgr->RegisterPayload(p.lua);
        if (id)
        {
            data.wardenData.registeredPayloadIds.push_back(id);
            payloadMgr->QueuePayload(id);
            LOG_DEBUG("module", "AnticheatPqr: Registered Warden payload {} ({}) for player {}",
                id, p.desc, player->GetName());
        }
    }

    data.wardenData.payloadsRegistered = true;
}

void AnticheatPqrMgr::TriggerWardenScan(Player* player, PlayerDetectionData& data)
{
    if (!_wardenEnabled || !data.wardenData.wardenInitialized)
        return;

    uint32 now = GameTime::GetGameTimeMS();

    // Respect scan interval cooldown
    if (data.wardenData.lastWardenScanTime > 0 &&
        now - data.wardenData.lastWardenScanTime < _wardenScanIntervalMs)
        return;

    WorldSession* session = player->GetSession();
    if (!session)
        return;

    Warden* warden = session->GetWarden();
    if (!warden || !warden->IsInitialized())
        return;

    warden->ForceChecks();
    data.wardenData.lastWardenScanTime = now;
    data.wardenData.wardenScanCount++;

    LOG_DEBUG("module", "AnticheatPqr: Forced Warden scan #{} for player {} (behavioral score: {:.1f})",
        data.wardenData.wardenScanCount, player->GetName(), data.overallScore);
}

void AnticheatPqrMgr::CleanupWardenPayloads(Player* player, PlayerDetectionData& data)
{
    if (!data.wardenData.payloadsRegistered)
        return;

    WorldSession* session = player->GetSession();
    if (!session)
        return;

    Warden* warden = session->GetWarden();
    if (!warden)
        return;

    WardenPayloadMgr* payloadMgr = warden->GetPayloadMgr();
    if (!payloadMgr)
        return;

    for (uint16 id : data.wardenData.registeredPayloadIds)
    {
        payloadMgr->UnregisterPayload(id);
    }

    LOG_DEBUG("module", "AnticheatPqr: Cleaned up {} Warden payloads for player {}",
        data.wardenData.registeredPayloadIds.size(), player->GetName());

    data.wardenData.registeredPayloadIds.clear();
    data.wardenData.payloadsRegistered = false;
}

// ============================================================================
// Script Implementations
// ============================================================================

class AnticheatPqrPlayerScript : public PlayerScript
{
public:
    AnticheatPqrPlayerScript() : PlayerScript("AnticheatPqrPlayerScript") { }

    void OnLogin(Player* player) override
    {
        sAnticheatPqrMgr->OnPlayerLogin(player);
    }

    void OnLogout(Player* player) override
    {
        sAnticheatPqrMgr->OnPlayerLogout(player);
    }

    void OnPlayerSpellCast(Player* player, Spell* spell, bool /*skipCheck*/) override
    {
        sAnticheatPqrMgr->OnSpellCast(player, spell);
    }

    void OnUpdate(Player* player, uint32 diff) override
    {
        sAnticheatPqrMgr->OnPlayerUpdate(player, diff);
    }
};

class AnticheatPqrWorldScript : public WorldScript
{
public:
    AnticheatPqrWorldScript() : WorldScript("AnticheatPqrWorldScript") { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        sAnticheatPqrMgr->LoadConfig();
    }

    void OnStartup() override
    {
        sAnticheatPqrMgr->Initialize();
    }
};

class AnticheatPqrCommandScript : public CommandScript
{
public:
    AnticheatPqrCommandScript() : CommandScript("AnticheatPqrCommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable anticheatPqrCommandTable =
        {
            { "status",     HandleAnticheatPqrStatusCommand,     SEC_GAMEMASTER, Console::No },
            { "check",      HandleAnticheatPqrCheckCommand,      SEC_GAMEMASTER, Console::No },
            { "reset",      HandleAnticheatPqrResetCommand,      SEC_GAMEMASTER, Console::No },
            { "wardenscan", HandleAnticheatPqrWardenScanCommand, SEC_GAMEMASTER, Console::No },
            { "reload",     HandleAnticheatPqrReloadCommand,     SEC_ADMINISTRATOR, Console::Yes },
        };

        static ChatCommandTable commandTable =
        {
            { "anticheatpqr", anticheatPqrCommandTable },
        };

        return commandTable;
    }

    static bool HandleAnticheatPqrStatusCommand(ChatHandler* handler)
    {
        handler->PSendSysMessage("AnticheatPqr Module Status: %s",
            sAnticheatPqrMgr->IsEnabled() ? "ENABLED" : "DISABLED");
        handler->PSendSysMessage("Warden Integration: %s",
            sAnticheatPqrMgr->IsWardenEnabled() ? "ENABLED" : "DISABLED");
        return true;
    }

    static bool HandleAnticheatPqrCheckCommand(ChatHandler* handler, Optional<PlayerIdentifier> target)
    {
        Player* player = nullptr;

        if (target)
            player = target->GetConnectedPlayer();
        else
            player = handler->getSelectedPlayer();

        if (!player)
        {
            handler->SendSysMessage("No player selected or found.");
            return false;
        }

        std::string report;
        if (sAnticheatPqrMgr->GetPlayerReport(player, report))
        {
            handler->PSendSysMessage("Report for %s:\n%s", player->GetName().c_str(), report.c_str());
        }
        else
        {
            handler->PSendSysMessage("No data available for player %s", player->GetName().c_str());
        }

        return true;
    }

    static bool HandleAnticheatPqrResetCommand(ChatHandler* handler, Optional<PlayerIdentifier> target)
    {
        Player* player = nullptr;

        if (target)
            player = target->GetConnectedPlayer();
        else
            player = handler->getSelectedPlayer();

        if (!player)
        {
            handler->SendSysMessage("No player selected or found.");
            return false;
        }

        sAnticheatPqrMgr->ResetPlayerData(player);
        handler->PSendSysMessage("Reset AnticheatPqr data for player %s", player->GetName().c_str());

        return true;
    }

    static bool HandleAnticheatPqrWardenScanCommand(ChatHandler* handler, Optional<PlayerIdentifier> target)
    {
        if (!sAnticheatPqrMgr->IsWardenEnabled())
        {
            handler->SendSysMessage("Warden integration is disabled.");
            return true;
        }

        Player* player = nullptr;

        if (target)
            player = target->GetConnectedPlayer();
        else
            player = handler->getSelectedPlayer();

        if (!player)
        {
            handler->SendSysMessage("No player selected or found.");
            return false;
        }

        WorldSession* session = player->GetSession();
        if (!session)
        {
            handler->SendSysMessage("Player session not available.");
            return false;
        }

        Warden* warden = session->GetWarden();
        if (!warden || !warden->IsInitialized())
        {
            handler->PSendSysMessage("Warden not initialized for player %s", player->GetName().c_str());
            return true;
        }

        warden->ForceChecks();
        handler->PSendSysMessage("Warden scan forced for player %s", player->GetName().c_str());
        return true;
    }

    static bool HandleAnticheatPqrReloadCommand(ChatHandler* handler)
    {
        sAnticheatPqrMgr->LoadConfig();
        handler->SendSysMessage("AnticheatPqr configuration reloaded.");
        return true;
    }
};

// ============================================================================
// Script Registration
// ============================================================================

void AddSC_mod_anticheat_pqr()
{
    new AnticheatPqrPlayerScript();
    new AnticheatPqrWorldScript();
    new AnticheatPqrCommandScript();
}
