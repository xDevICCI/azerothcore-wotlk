-- ============================================================================
-- RBAC Role Definitions
-- ============================================================================

DROP TABLE IF EXISTS `rbac_roles`;
CREATE TABLE `rbac_roles` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `name` VARCHAR(128) NOT NULL DEFAULT ''
        COMMENT 'Human-readable role name',
    PRIMARY KEY (`id`),
    UNIQUE KEY `idx_name` (`name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
    COMMENT='RBAC named role definitions';

-- Pre-populated example roles
INSERT INTO `rbac_roles` (`id`, `name`) VALUES
(1, 'Chat Moderator'),
(2, 'Event Manager'),
(3, 'Support Staff');
