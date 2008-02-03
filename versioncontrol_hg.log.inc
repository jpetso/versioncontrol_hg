<?php
// $Id$

/**
 * @file
 * Mercurial backend for Version Control API - Provides Mercurial commit
 * information and account management as a pluggable backend.
 *
 * This file defines internal functions for processing logs.
 *
 * Copyright 2008 by Edward Z. Yang (ezyang, http://drupal.org/user/211688)
 */

/**
 * Update repository's database tables by interfacing with Mercurial.
 *
 * @warning
 *    This code should not directly interface with the Mercurial
 *    executable; the hg_* functions defined in hg/hg.inc should be used
 *    instead. If you need functionality not yet defined, submit a patch
 *    for hg/hg.inc implementing it.
 *
 * @warning
 *    This function does not account for memory exhaustion.
 *    An alternative implementation could read each log entry one by
 *    one from the file (as CVS's implementation does), however, the
 *    resulting data-structure would still cause large amounts of memory
 *    usage (this is a primary deficiency of CVS) and would require
 *    multiple database queries. After determining which heuristics
 *    need to be able to access previous and later log entries, we will
 *    refactor accordingly; an alternate implementation would be to
 *    limit rev_range so that only batches of the logs are parsed at
 *    a time. Nevertheless, real world usage data is necessary before
 *    we can make a decision in this respect.
 *
 * @param $repository
 *    Repository variable as returned by versioncontrol_get_repository()
 */
function _versioncontrol_hg_log_update_repository(&$repository) {
  
  // Because we only support local repositories, `hg pull` is NOT called
  // before we perform these changes. It is the responsibility of all
  // branch maintainers to push their changes to this repository so that
  // their changes are registered.
  
  $latest_rev = $repository['hg_specific']['latest_rev'];
  if ($latest_rev === false) {
    // This is necessary to ensure that the revisions are ordered
    // properly, from earliest to latest. We need logs in this order
    // because some algorithms reference earlier information.
    $rev_range = '0:';
  } else {
    $rev_range = ($latest_rev + 1) .':';
  }
  $raw_logs = hg_log($repository['root'], $rev_range);
  // watchdog('special', hg_last_cmd());
  
  if (empty($raw_logs)) {
    // No new logs, abort.
    return;
  }
  
  _versioncontrol_hg_log_preprocess($raw_logs);
  
  // We could refactor each of these into a "handle" function, e.g.
  // _versioncontrol_hg_log_handle_commits, which performs parsing and
  // database work.
  
  // Another possible refactoring is to consider each log entry
  // separately; this allows us to process extremely large data-sets
  // without running out of memory, and also lets analysis functions
  // refer to previous revisions with impunity by calling the database
  // (or we could set up a cache).
  
  // We currently separate database updates from parameter generation.
  // This is nice, but requires a little redundant code.
  
  // This must run before commits, as it depends on new branches
  // having not been auto-generated.
  $raw_branch_operations = _versioncontrol_hg_log_parse_branch_operations($raw_logs, $repository);
  $raw_commits = _versioncontrol_hg_log_parse_commits($raw_logs, $repository);

  // Update the database:
  foreach ($raw_branch_operations as $raw_branch_operation) {
    list ($branch, $branched_items) = $raw_branch_operation;
    versioncontrol_insert_branch_operation($branch, $branched_items);
  }
  foreach ($raw_commits as $raw_commit) {
    list ($commit, $commit_actions) = $raw_commit;
    versioncontrol_insert_commit($commit, $commit_actions);
  }
  
  // Update latest revision
  $latest_rev = _versioncontrol_hg_log_parse_latest_rev($raw_logs);
  $repository['hg_specific']['latest_rev'] = $latest_rev;
  db_query('UPDATE {versioncontrol_hg_repositories}
            SET latest_rev = %d WHERE repo_id = %d',
            $repository['hg_specific']['latest_rev'], $repository['repo_id']);
  
}

/**
 * Perform common pre-processing on logs from hg_logs(), although no
 * major changes to the array's structure.
 *
 * @warning
 *    This function MUST NOT be lossy or fundamentally change the log
 *    structure; any results of this pre-processing should be stored in
 *    unique keys prefixed with an underscore, so as to prevent collisions
 *    with the keys specified in hg/templates/changeset.tmpl.
 *
 * @param
 *    Raw logs variable to pre-process. The following additional indexes
 *    will be added:
 *
 *      '_date' => String UTC Unix timestamp of changeset
 *      '_offset' => Integer time zone offset of commiter
 *      '_manifest' => Array parent nodeid
 *      '_parents' => array(0 => First parent nodeid, 1 => Second parent nodeid)
 *      '_author' => String author name w/o email
 *      '_email' => String author's email
 *      '_branch' => Either the contents of branches, or 'default'
 *
 *    Nodeids take the form of array(string revision number, string SHA-1 hash)
 */
function _versioncontrol_hg_log_preprocess(&$raw_logs) {
  
  for ($i = 0, $c = count($raw_logs); $i < $c; $i++) {
    $log =& $raw_logs[$i];
    
    // Parse Mercurial's log format into Unix timestamp and offset.
    // We do NOT convert the timestamp into an integer to prevent overflow
    list($log['_date'], $offset) = explode('.', $log['date']);
    $log['_offset'] = (int) $offset;
    
    // Split manifest nodeid (note that we are not converting the revision
    // number to an integer either.)
    $log['_manifest'] = explode(':', $log['manifest']);
    
    // Split parents, and then split their nodeids
    $parents = explode(' ', $log['parents']);
    foreach ($parents as $k => $v) {
      $log['_parents'][$k] = explode(':', $v);
    }
    
    // Parse author into author name and email
    // This is somewhat fragile, but should work as long as a user does
    // not have '>' in their name! :-)
    $bits = explode('<', $log['author']);
    $log['_author'] = rtrim($bits[0]);
    if (!empty($bits[1])) {
      $log['_email'] = rtrim($bits[1], '>');
    }
    else {
      $log['_email'] = null;
    }
    
    $log['_branch'] = ($log['branches'] !== '') ? $log['branches'] : 'default';
  }
  
}

/**
 * Parses the array output of hg_log() into form ready for
 * versioncontrol_insert_commit().
 *
 * @warning
 *    Some really black-magicky path munging is going on in here to
 *    accomodate commitlog. I'm not sure if this is proper or we've
 *    got an upstream bug on our hands.
 *
 * @param $raw_logs
 *    Array from hg_log() to be parsed.
 * @param $repository
 *    Repository array as per versioncontrol_get_repository().
 * @return
 *    Array list, with items in form of array(0 => $commit, 1 => $commit_actions)
 *    which are ready for insertion using versioncontrol_insert_commit().
 */
function _versioncontrol_hg_log_parse_commits($raw_logs, $repository) {
  $ret = array();
  
  foreach ($raw_logs as $log) {
    // Each log entry corresponds to a commit.
    
    // We use a "raw commit" structure to contain both parameters to
    // versioncontrol_insert_commit()
    $raw_commit = array(array(), array());
    $commit =& $raw_commit[0];
    $commit_actions =& $raw_commit[1];
    
    // Build $commit:
    $commit = _versioncontrol_hg_log_parse_operation($log, $repository);
    $commit['message'] = $log['desc'];
    $commit['revision'] = $log['node'];
    $commit['hg_specific'] = array(
      'branch_id' => versioncontrol_ensure_branch($log['_branch'], $repository['repo_id']),
      'rev' => $log['rev'],
      // Using key [1] to retrieve only SHA-1 hash; the revision number
      // should be the same as 'rev'
      'manifest' => $log['_manifest'][1],
      // We'll need this in order to look up the vc_op_id. In
      // this case, we use revision because it's easier to handle and
      // this is all internal processing.
      'parent1' => $log['_parents'][0][0],
      'parent2' => $log['_parents'][1][0],
    );
    
    // Build $commit_actions:
    
    // Under very special conditions (basically, when a merge adds
    // a file, but no edits were made to it), file_adds can contain
    // files that are not in files. So, we need to merge these two
    // arrays.
    $files = array_unique(array_merge($log['files'], $log['file_adds']));
    
    // These lookups must be tested with isset(), NOT !empty()
    $lookup_adds   = array_flip($log['file_adds']);
    $lookup_dels   = array_flip($log['file_dels']);
    $lookup_copies = array_flip($log['file_copies']);
    foreach ($files as $raw_file) {
      $commit_action = array();
      
      // Normalize Mercurial's file declaration "CVS" style to accomodate
      // commitlog.
      $file = "/$raw_file";
      
      // CVS's implementation involves building a large array with
      // everything you could possibly need, and then excising things
      // based on options. While that approach may take less code,
      // it is less robust to change.
      
      // We need to use $raw_file to reference items in the logs.
      if (isset($lookup_adds[$raw_file])) {
        $commit_action['action'] = VERSIONCONTROL_ACTION_ADDED;
      }
      elseif (isset($lookup_dels[$raw_file])) {
        $commit_action['action'] = VERSIONCONTROL_ACTION_DELETED;
      }
      elseif (isset($lookup_copies[$raw_file])) {
        // This doesn't ever seem to happen.
        $commit_action['action'] = VERSIONCONTROL_ACTION_COPIED;
      }
      else {
        // File was modified
        $commit_action['action'] = VERSIONCONTROL_ACTION_MODIFIED;
      }
      
      // Logs do not have information for 'modified', because
      // file_copies is never filled with anything.
      $commit_action['modified'] = FALSE;
      
      $commit_action['current item'] = array();
      
      // This isn't actually used, but we set it for completeness-sake.
      $commit_action['current item']['path'] = $file;
      
      if ($commit_action['action'] != VERSIONCONTROL_ACTION_DELETED) {
        $commit_action['current item']['type'] = VERSIONCONTROL_ITEM_FILE;
      }
      else {
        $commit_action['current item']['type'] = VERSIONCONTROL_ITEM_FILE_DELETED;
      }
      
      // This will be set later in versioncontrol_hg_commit(), because we 
      // need to ask the database for some information (the log doesn't tell
      // us very much about this.)
      $commit_action['source items'] = array();
      
      $commit_actions[$file] = $commit_action;
      
    }
    
    $ret[] = $raw_commit;
  }
  
  return $ret;
}

/**
 * Parse the array output of hg_log() into form ready for
 * versioncontrol_insert_branch_operation().
 *
 * @return
 *    Array in form of array($branch, $branched_items = array()), with
 *    variables corresponding to parameters of versioncontrol_insert_branch_operation() 
 */
function _versioncontrol_hg_log_parse_branch_operations($raw_logs, $repository) {
  $ret = array();
  foreach ($raw_logs as $log) {
    $branch_id = versioncontrol_get_branch_id($log['_branch'], $repository['repo_id'], TRUE);
    if ($branch_id === null) {
      $commit = _versioncontrol_hg_log_parse_operation($log, $repository);
      $commit['branch_name'] = $log['_branch'];
      $commit['action'] = VERSIONCONTROL_ACTION_ADDED;
      // All items are affected, so there's no need to check.
      $ret[] = array($commit, array());
    }
    // Generate the branch for posterity!
    versioncontrol_ensure_branch($log['_branch'], $repository['repo_id']);
  }
  return $ret;
}

/**
 * Returns an operation array with the common values for all types
 * of operations (commit, branch, tag) instantiated.
 */
function _versioncontrol_hg_log_parse_operation($log, $repository) {
  return array(
    'repository' => $repository,
    'date' => $log['_date'],
    'username' => $log['_email'],
  );
}

/**
 * Determines the latest revision of a series of log entries.
 */
function _versioncontrol_hg_log_parse_latest_rev($raw_logs) {
  $max_rev = 0;
  foreach ($raw_logs as $log) {
    if ($log['rev'] > $max_rev) {
      $max_rev = $log['rev'];
    }
  }
  return $max_rev;
}

