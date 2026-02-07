-- Solo Queue Arena Master NPC
-- Spawn in-game with: .npc add 200000

DELETE FROM `creature_template` WHERE `entry` = 200000;
INSERT INTO `creature_template` (`entry`, `modelid1`, `name`, `subname`, `IconName`, `minlevel`, `maxlevel`,
    `faction`, `npcflag`, `speed_walk`, `speed_run`, `scale`, `unit_class`, `unit_flags`, `unit_flags2`,
    `type`, `type_flags`, `ScriptName`) VALUES
(200000, 31841, 'Arena Solo Queue', 'Solo Queue Master', 'Directions', 80, 80,
    35, 1, 1, 1.14286, 1, 1, 2, 2048,
    7, 0, 'NpcSoloQueue');
