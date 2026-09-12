-- Weekend 70 FFA schedule key (77) documented for operators.
-- Sun..Sat CSV: 0=rest, 10/50/70=per-job tiers, 77=70大乱斗 (all jobs, 70 map).
-- Default suggestion: Sat+Sun=77. Existing rows are NOT overwritten.
-- FFA rewards: server_arena_rewards job=0, bracket=77, rank_pos=1..5.

-- Optional: enable weekend FFA on an existing install (edit as needed):
-- UPDATE server_arena_config
-- SET weekday_brackets = '77,70,50,10,70,50,77',
--     weekday_mask = 127
-- WHERE id = 1;
