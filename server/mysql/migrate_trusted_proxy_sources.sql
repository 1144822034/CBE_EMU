-- 后台“安全设置”中管理可信反向代理 TCP 来源及其可采信请求头。
-- 仅允许单 IPv4 的 X-Real-IP / X-Forwarded-For 值；服务端不会解析转发链。

CREATE TABLE IF NOT EXISTS `server_trusted_proxy_sources` (
  `source_ip` VARCHAR(15) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `trust_x_real_ip` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `trust_x_forwarded_for` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`source_ip`)
) ENGINE=InnoDB;

INSERT IGNORE INTO `server_trusted_proxy_sources`
  (`source_ip`, `trust_x_real_ip`, `trust_x_forwarded_for`, `enabled`)
VALUES
  ('127.0.0.1', 1, 1, 1);
