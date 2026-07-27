# Level Designation Catalog

## Trigger and Expected Behaviour

Opening the in-scene title page issues WT 1/23/1. Previously its response
contained only the money-based riches_name*.gif rows, so a role could never
view or select level-based titles such as 小试牛刀 or 开山鼻祖.

The page must list every title currently unlocked by the role, preserve a
previously selected valid title, and send the selected actor title through the
existing 1/23/3 confirmation plus 1/23/2 scene-node update path.

## Evidence

- HandleDesignationInfoResponse(0x0102A93E) parses a raw designationinfo list
  whose first tagged byte is sent back as WT 1/23/3 field type.
- net_handle_designationinfo_update(0x01010DB6) consumes the same title ID,
  short title and overhead resource in a 1/23/2 actor update.
- bin/JHOnlineData/level_name.gif is a badge strip. Its matching individual
  resources level_name0.gif through level_name12.gif recover the thirteen
  exact title/resource mappings in
  [Role Designation Page](2026-07-02-role-designation-page.md).
- The local resource at level_name1.gif reads 初学乍练; 初来乍到 occurs in task
  resources, so it must not be substituted into a title-resource row.
- The client page parser receives only already-unlocked rows and sends back only
  a selected ID. It does not carry an eligibility threshold, so the resource
  files prove the name/resource mapping but not the server-side level gate.

## First Divergence and Root Cause

The original implementation had a static ten-row wealth-only catalog. Its
unlock predicate tested only role->money, and normalization also treated the
designation ID as an array-style range (id < 10). Therefore the client
received no valid rows for the recovered level badges, and any future sparse
title ID would have been reset before it reached the parser.

The client parser and scene update packet were already correct; the first
contract violation was the server-side catalog and eligibility model.

## Correction

- Added a minLevel eligibility field and the recovered level-title rows with
  stable IDs 16..28; the existing wealth IDs 0..9 are unchanged. The current
  server policy is level 1, then each five levels through level 60. This is an
  explicit progression rule inferred from the 13-row level resource sequence,
  not a claim that the client parser supplied those thresholds.
- Evaluate title eligibility from both persisted money and derived level.
  Normalization now derives level from persisted EXP before deciding whether
  the stored designation remains unlocked.
- Title lookup now reports an unknown protocol ID instead of silently mapping
  it to 一贫如洗.
- Expanded the 1/23/1 raw-list staging buffer for the full 23-row catalog.
- Kept fieldB=0 for all rows because the existing client render evidence shows
  a nonzero second title byte is unsafe.

## Verification

scripts/role-designation-level-regression.php seeds a level-60, zero-money
role, opens 1/23/1, verifies all thirteen level title rows and their real
badge resources are present, selects ID 28, and confirms both the
1/23/3 + 1/23/2 response and the persisted designation ID.

The test also attempts to select the locked level-50 title from a level-1 role
and verifies the server returns only result=0, without a scene-node update.

## Remaining Scope

The exact original-production threshold table still requires a captured
original-server 23/1 response (or original server data). Other title families
need equivalent resource/protocol evidence before they are added; this change
does not invent placeholder title or badge names.
