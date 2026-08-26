USE `jh_online`;

-- Retired migration.  Legacy server_admin_config bootstrap data is no longer
-- created or read by the service.  Provision the first operator explicitly
-- with migrate_admin_users.sql and an INSERT into server_admin_users.
SELECT 'migrate_add_web_accounts.sql is retired; no legacy admin data was created' AS migration_status;
