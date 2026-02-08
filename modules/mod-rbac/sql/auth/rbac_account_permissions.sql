-- ============================================================================
-- RBAC Per-Account Permission Grants/Denies
-- Stored in auth database (per-account, cross-realm)
-- ============================================================================

DROP TABLE IF EXISTS `rbac_account_permissions`;
CREATE TABLE `rbac_account_permissions` (
    `accountId` INT UNSIGNED NOT NULL,
    `permModule` VARCHAR(64) NOT NULL DEFAULT 'core',
    `permId` INT UNSIGNED NOT NULL,
    `granted` TINYINT(1) NOT NULL DEFAULT 1
        COMMENT '1=granted, 0=denied',
    `realmId` INT NOT NULL DEFAULT -1
        COMMENT '-1=all realms, else specific realm',
    PRIMARY KEY (`accountId`, `permModule`, `permId`, `realmId`),
    KEY `idx_account` (`accountId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
    COMMENT='Per-account RBAC permission grants/denies';
