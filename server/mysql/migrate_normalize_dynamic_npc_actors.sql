USE `jh_online`;

/* n_girl.actor can be decoded as an Actor file, but the CBE client cannot
 * safely consume it as a dynamic 27/11 NPC model.  Preserve every NPC row,
 * script and position; only replace the invalid dynamic-model setting. */
START TRANSACTION;

UPDATE `server_dynamic_npcs`
SET `actor_resource` = 'n_woman1.actor'
WHERE `actor_resource` = 'n_girl.actor';

COMMIT;
