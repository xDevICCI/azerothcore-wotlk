-- ============================================================================
-- Warden checks for PQR bot detection
-- Module: mod-anticheat-pqr
-- ============================================================================

-- MODULE_CHECK (type 217) - Detect known PQR injected DLLs
-- These checks verify if specific DLLs are loaded in the client process
DELETE FROM `warden_checks` WHERE `id` BETWEEN 900 AND 905;
INSERT INTO `warden_checks` (`id`, `type`, `data`, `str`, `address`, `length`, `result`, `comment`) VALUES
(900, 217, '', 'PQR.DLL',          0, 0, '', 'PQR bot - main DLL'),
(901, 217, '', 'PQROTATION.DLL',   0, 0, '', 'PQR rotation engine DLL'),
(902, 217, '', 'PQRINTERFACE.DLL', 0, 0, '', 'PQR interface DLL'),
(903, 217, '', 'LUARUNTIME.DLL',   0, 0, '', 'PQR Lua runtime injection'),
(904, 217, '', 'PQRLOADER.DLL',    0, 0, '', 'PQR loader DLL'),
(905, 217, '', 'FISHBOT.DLL',      0, 0, '', 'Fish bot injection DLL');

-- LUA_EVAL_CHECK (type 139) - Detect PQR Lua global variables
-- These checks evaluate Lua code on the client to detect PQR globals
-- Note: Existing checks 789-796 already cover PQR_IsMoving and chat detection
DELETE FROM `warden_checks` WHERE `id` BETWEEN 920 AND 925;
INSERT INTO `warden_checks` (`id`, `type`, `data`, `str`, `address`, `length`, `result`, `comment`) VALUES
(920, 139, NULL, 'return not not PQR_Addon',          NULL, NULL, NULL, 'PQR addon global'),
(921, 139, NULL, 'return not not PQR_Pause',           NULL, NULL, NULL, 'PQR pause function'),
(922, 139, NULL, 'return not not PQR_WriteToChat',     NULL, NULL, NULL, 'PQR chat writer'),
(923, 139, NULL, 'return not not PQR_EventMonitor',    NULL, NULL, NULL, 'PQR event monitor'),
(924, 139, NULL, 'return not not PQR_TotalIterations', NULL, NULL, NULL, 'PQR iteration counter'),
(925, 139, NULL, 'return not not UnlockForbidden',     NULL, NULL, NULL, 'Lua unlocker function');

-- ============================================================================
-- Warden action overrides (for acore_characters database)
-- Action values: 0 = LOG only, 1 = KICK, 2 = BAN
-- Default to LOG so GMs can investigate before escalating
-- To apply these, run against acore_characters database:
-- ============================================================================
-- DELETE FROM `warden_action` WHERE `wardenId` BETWEEN 900 AND 905;
-- DELETE FROM `warden_action` WHERE `wardenId` BETWEEN 920 AND 925;
-- INSERT INTO `warden_action` (`wardenId`, `action`) VALUES
-- (900, 0), (901, 0), (902, 0), (903, 0), (904, 0), (905, 0),
-- (920, 0), (921, 0), (922, 0), (923, 0), (924, 0), (925, 0);
