-- ============================================================================
-- RBAC Per-Account Role Assignments
-- Stored in auth database (per-account, cross-realm)
-- ============================================================================

DROP TABLE IF EXISTS `rbac_account_roles`;
CREATE TABLE `rbac_account_roles` (
    `accountId` INT UNSIGNED NOT NULL,
    `roleId` INT UNSIGNED NOT NULL,
    `realmId` INT NOT NULL DEFAULT -1
        COMMENT '-1=all realms, else specific realm',
    PRIMARY KEY (`accountId`, `roleId`, `realmId`),
    KEY `idx_account` (`accountId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
    COMMENT='Per-account RBAC role assignments';
