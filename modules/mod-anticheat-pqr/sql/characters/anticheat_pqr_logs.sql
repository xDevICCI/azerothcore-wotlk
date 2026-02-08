-- --------------------------------------------------------
-- AnticheatPqr Module - Character Database Schema
-- --------------------------------------------------------

DROP TABLE IF EXISTS `anticheat_pqr_logs`;

CREATE TABLE `anticheat_pqr_logs` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `guid` INT UNSIGNED NOT NULL COMMENT 'Character GUID',
    `account_id` INT UNSIGNED NOT NULL COMMENT 'Account ID',
    `player_name` VARCHAR(50) NOT NULL,
    `overall_score` FLOAT NOT NULL DEFAULT 0,
    `timing_score` FLOAT NOT NULL DEFAULT 0,
    `reaction_score` FLOAT NOT NULL DEFAULT 0,
    `pattern_score` FLOAT NOT NULL DEFAULT 0,
    `total_casts` INT UNSIGNED NOT NULL DEFAULT 0,
    `suspicious_timings` INT UNSIGNED NOT NULL DEFAULT 0,
    `inhuman_reactions` INT UNSIGNED NOT NULL DEFAULT 0,
    `alert_level` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=none, 1=low, 2=medium, 3=high',
    `reason` VARCHAR(255) NOT NULL DEFAULT '',
    `timestamp` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    KEY `idx_guid` (`guid`),
    KEY `idx_account_id` (`account_id`),
    KEY `idx_timestamp` (`timestamp`),
    KEY `idx_alert_level` (`alert_level`),
    KEY `idx_overall_score` (`overall_score`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- --------------------------------------------------------
-- Aggregated statistics per player (optional, for reporting)
-- --------------------------------------------------------

DROP TABLE IF EXISTS `anticheat_pqr_stats`;

CREATE TABLE `anticheat_pqr_stats` (
    `guid` INT UNSIGNED NOT NULL COMMENT 'Character GUID',
    `account_id` INT UNSIGNED NOT NULL COMMENT 'Account ID',
    `player_name` VARCHAR(50) NOT NULL,
    `total_sessions` INT UNSIGNED NOT NULL DEFAULT 0,
    `total_alerts` INT UNSIGNED NOT NULL DEFAULT 0,
    `max_overall_score` FLOAT NOT NULL DEFAULT 0,
    `avg_overall_score` FLOAT NOT NULL DEFAULT 0,
    `last_alert_time` DATETIME DEFAULT NULL,
    `first_seen` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `last_seen` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    `is_flagged` TINYINT(1) NOT NULL DEFAULT 0 COMMENT 'Manual flag by GM',
    `is_whitelisted` TINYINT(1) NOT NULL DEFAULT 0 COMMENT 'Exempt from detection',
    `notes` TEXT DEFAULT NULL COMMENT 'GM notes',
    PRIMARY KEY (`guid`),
    KEY `idx_account_id` (`account_id`),
    KEY `idx_max_score` (`max_overall_score`),
    KEY `idx_flagged` (`is_flagged`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- --------------------------------------------------------
-- View for easy reporting
-- --------------------------------------------------------

CREATE OR REPLACE VIEW `v_anticheat_pqr_suspicious` AS
SELECT
    l.player_name,
    l.account_id,
    l.overall_score,
    l.timing_score,
    l.reaction_score,
    l.pattern_score,
    l.alert_level,
    l.reason,
    l.timestamp,
    s.total_alerts,
    s.is_flagged,
    s.is_whitelisted
FROM `anticheat_pqr_logs` l
LEFT JOIN `anticheat_pqr_stats` s ON l.guid = s.guid
WHERE l.alert_level >= 2
ORDER BY l.timestamp DESC;
