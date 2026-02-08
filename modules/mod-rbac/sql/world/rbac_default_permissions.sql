-- ============================================================================
-- RBAC Default Permissions per Security Level
-- These are automatically applied based on account security level
-- ============================================================================

DROP TABLE IF EXISTS `rbac_default_permissions`;
CREATE TABLE `rbac_default_permissions` (
    `securityLevel` TINYINT UNSIGNED NOT NULL
        COMMENT '0=Player, 1=Moderator, 2=GM, 3=Admin',
    `permModule` VARCHAR(64) NOT NULL DEFAULT 'core',
    `permId` INT UNSIGNED NOT NULL,
    PRIMARY KEY (`securityLevel`, `permModule`, `permId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
    COMMENT='Default RBAC permissions per security level';

-- Note: This table is OPTIONAL. If empty, the module falls through
-- to the standard security level check for all commands.
-- Only populate this if you want to restrict specific commands
-- from a security level (by NOT including them in their defaults).
