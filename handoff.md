cd /home/noneya/code/donix
git status
git diff --stat
git add handoff.md
git status
git commit -F - <<'EOF'
handoff: hand repo state back to git; stop hand-writing the header

The header named HEAD, the ahead/behind count, the untracked file,
and the live scratch-tag list.  Every one of those is invalidated
by the commit that writes it: correct the header, commit, and the
new commit's own SHA makes it stale again.  That is structural, not
a discipline problem, and it recurred three times in one session.

The header now carries narrative only, plus a four-command
repo-state block the reader runs.  Also fixed in the same spirit:

- the "Eight scratch tags are live" paragraph, which listed state,
  becomes a "run git tag" pointer;
- "State on disk" no longer calls PFcapture.txt untracked (it is
  gitignored; the line now says to ls it);
- "How to use this file" says rewrite the narrative, not repo
  state.

No kernel change.  No tag.  One change at a time.
EOF
git log --oneline -3
