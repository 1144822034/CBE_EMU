USE `jh_online`;

-- Admin monster display-name override (GBK). Empty = keep SCE/catalog label.
ALTER TABLE `server_monsters`
  ADD COLUMN `display_name` VARBINARY(31) NOT NULL DEFAULT ''
  AFTER `actor_resource`;
