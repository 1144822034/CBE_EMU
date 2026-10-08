-- Equipment enhance gold cost per step (admin; default 5, product formula).
-- gold = (currentEnhanceLevel + 1) * enhance_gold_per_step
-- copper = gold * 10000.  0 = free.  Does not change wash / recast fees.

ALTER TABLE `server_enhance_wash_settings`
  ADD COLUMN `enhance_gold_per_step` INT UNSIGNED NOT NULL DEFAULT 5
    COMMENT '29/2|3 gold per step; cost=(level+1)*this; 0=free'
  AFTER `transfer_money_enabled`;

-- Runtime also probes information_schema and ALTERs if the column is missing.
