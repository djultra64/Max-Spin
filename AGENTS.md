# Max Spin project instructions

## Feature parity

Linux and Windows must offer the same user-facing features in every new release.
Implement and test both launch paths before shipping a feature. Keep one shared
athlete-package format and validator; platform adapters must preserve the same
slot selection, validation, fallbacks, frozen-session behavior and rollback.
Do not ship a feature on only one platform or treat compilation as functional
parity. Obtain an explicit user exception before shipping any platform gap.

Update FEATURES.json when introducing or changing a user-facing feature and run
tools/check_feature_parity.py. The packager refuses unequal feature sets or
revisions and records the matching capabilities in both release manifests.
Exercise actual platform behavior with appropriate CPU/integration tests. The
declaration is a release gate, not a substitute for functional tests. The user
performs gameplay tests; do not launch, control or close their game sessions.

## Attribution and releases

Attribute the user's own work exclusively to djultra64. Verify repository Git
author and committer identity before committing or publishing. Preserve genuine
third-party authors and required notices. Publish normal GitHub releases and
verify prerelease is false afterward.

## Controller behavior

Keep logical actions separate from physical buttons. Xbox, PlayStation and
Nintendo Switch prompts must follow the active controller's real layout,
including Nintendo A/B and X/Y positions. Preserve optional action remapping,
automatic last-device switching and a manual controller-family fallback.
