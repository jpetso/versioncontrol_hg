-- This script resets all registered repositories.
-- Run this, then run cron, to get a clean read-out of all your Mercurial repositories.
DELETE FROM versioncontrol_commits
  WHERE EXISTS (
    SELECT * FROM versioncontrol_hg_commits hg WHERE versioncontrol_commits.vc_op_id = hg.vc_op_id
  );
TRUNCATE versioncontrol_hg_commits;
TRUNCATE versioncontrol_hg_commit_actions;
TRUNCATE versioncontrol_hg_tags;
UPDATE versioncontrol_hg_repositories SET latest_rev = NULL;