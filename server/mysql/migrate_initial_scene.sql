USE `jh_online`;

-- Canonicalize only the Penglai starting-scene aliases. Other saved maps and
-- coordinates are deliberately left untouched. Scene strings are GBK bytes.
UPDATE `account_roles`
SET `scene` = X'633030C5EEC0B3CFC9B5BA5F30312E736365'
WHERE `scene` IN (
  X'30305FC5EEC0B3CFC9B5BA3031',
  X'30305FC5EEC0B3CFC9B5BA30312E736365',
  X'633030C5EEC0B3CFC9B5BA5F3031'
);
