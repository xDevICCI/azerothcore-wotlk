-- ============================================================================
-- RBAC Role-Permission Mappings
-- ============================================================================

DROP TABLE IF EXISTS `rbac_role_permissions`;
CREATE TABLE `rbac_role_permissions` (
    `roleId` INT UNSIGNED NOT NULL
        COMMENT 'References rbac_roles.id',
    `permModule` VARCHAR(64) NOT NULL DEFAULT 'core'
        COMMENT 'Permission module namespace',
    `permId` INT UNSIGNED NOT NULL
        COMMENT 'Permission ID within module',
    PRIMARY KEY (`roleId`, `permModule`, `permId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
    COMMENT='Maps roles to permissions';

-- Chat Moderator role: kick, mute, unmute, pinfo
INSERT INTO `rbac_role_permissions` (`roleId`, `permModule`, `permId`) VALUES
(1, 'core', 60),  -- kick
(1, 'core', 61),  -- mute
(1, 'core', 62),  -- unmute
(1, 'core', 65);  -- pinfo

-- Event Manager role: teleport, announce, npc, additem
INSERT INTO `rbac_role_permissions` (`roleId`, `permModule`, `permId`) VALUES
(2, 'core', 20),  -- teleport
(2, 'core', 63),  -- announce
(2, 'core', 50),  -- npc
(2, 'core', 64);  -- additem

-- Support Staff role: lookup, pinfo, revive
INSERT INTO `rbac_role_permissions` (`roleId`, `permModule`, `permId`) VALUES
(3, 'core', 30),  -- lookup
(3, 'core', 65),  -- pinfo
(3, 'core', 66);  -- revive
