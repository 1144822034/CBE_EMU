-- 赏金擂台：摆擂消耗活力（撤擂不退）；运行时 mock_server_arena.c 也会 ALTER。
ALTER TABLE `server_arena_config`
  ADD COLUMN `wager_vitality_cost` INT UNSIGNED NOT NULL DEFAULT 50
  COMMENT '摆擂消耗活力；撤擂不退'
  AFTER `wager_currency`;
