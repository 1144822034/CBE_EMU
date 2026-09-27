-- Pixi：宣阳广场天下第一雕像每日膜拜次数
CREATE TABLE IF NOT EXISTS account_role_tianxia_worship_daily (
  role_id INT UNSIGNED NOT NULL,
  day_ymd INT UNSIGNED NOT NULL,
  used_count TINYINT UNSIGNED NOT NULL DEFAULT 0,
  updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
    ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (role_id, day_ymd)
) ENGINE=InnoDB;
