-- ============================================================================
-- RBAC Permission Definitions
-- Uses module_string composite-key pattern: (module, id)
-- Each module namespace has its own independent ID space
-- ============================================================================

DROP TABLE IF EXISTS `rbac_permissions`;
CREATE TABLE `rbac_permissions` (
    `module` VARCHAR(64) NOT NULL DEFAULT 'core'
        COMMENT 'Module namespace (e.g., core, mod-guild-ban)',
    `id` INT UNSIGNED NOT NULL
        COMMENT 'Permission ID within the module',
    `name` VARCHAR(128) NOT NULL DEFAULT ''
        COMMENT 'Human-readable permission name',
    `linked_command` VARCHAR(128) NOT NULL DEFAULT ''
        COMMENT 'Dot-command name (e.g., ban account). Empty if not command-linked.',
    PRIMARY KEY (`module`, `id`),
    KEY `idx_linked_command` (`linked_command`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
    COMMENT='RBAC permission definitions (module_string pattern)';

-- Pre-populated core command permissions (examples)
INSERT INTO `rbac_permissions` (`module`, `id`, `name`, `linked_command`) VALUES
-- GM commands
('core', 1,  'Command: gm',              'gm'),
('core', 2,  'Command: gm fly',          'gm fly'),
('core', 3,  'Command: gm visible',      'gm visible'),
-- Ban commands
('core', 10, 'Command: ban account',      'ban account'),
('core', 11, 'Command: ban character',    'ban character'),
('core', 12, 'Command: ban ip',           'ban ip'),
('core', 13, 'Command: unban account',    'unban account'),
('core', 14, 'Command: unban character',  'unban character'),
('core', 15, 'Command: unban ip',         'unban ip'),
-- Teleport commands
('core', 20, 'Command: teleport',         'teleport'),
('core', 21, 'Command: teleport name',    'teleport name'),
('core', 22, 'Command: teleport group',   'teleport group'),
-- Lookup commands
('core', 30, 'Command: lookup',           'lookup'),
('core', 31, 'Command: lookup player',    'lookup player'),
('core', 32, 'Command: lookup item',      'lookup item'),
-- Modify commands
('core', 40, 'Command: modify',           'modify'),
('core', 41, 'Command: modify speed',     'modify speed'),
('core', 42, 'Command: modify money',     'modify money'),
('core', 43, 'Command: modify hp',        'modify hp'),
-- NPC commands
('core', 50, 'Command: npc',              'npc'),
('core', 51, 'Command: npc add',          'npc add'),
('core', 52, 'Command: npc info',         'npc info'),
-- Misc GM commands
('core', 60, 'Command: kick',             'kick'),
('core', 61, 'Command: mute',             'mute'),
('core', 62, 'Command: unmute',           'unmute'),
('core', 63, 'Command: announce',         'announce'),
('core', 64, 'Command: additem',          'additem'),
('core', 65, 'Command: pinfo',            'pinfo'),
('core', 66, 'Command: revive',           'revive'),
('core', 67, 'Command: die',              'die');
