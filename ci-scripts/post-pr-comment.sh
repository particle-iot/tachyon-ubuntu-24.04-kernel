#!/bin/bash
set -e

# post-pr-comment.sh - Post a PR comment with artifact links
# This script generates a comment with links to all built .deb artifacts

if [ -z "$CIRCLE_PULL_REQUEST" ]; then
    echo "Not a PR build, skipping comment"
    exit 0
fi

if [ -z "$CIRCLE_BUILD_NUM" ]; then
    echo "ERROR: CIRCLE_BUILD_NUM not set"
    exit 1
fi

# Extract PR number from CIRCLE_PULL_REQUEST
# Format: https://github.com/owner/repo/pull/123
PR_NUMBER=$(echo "$CIRCLE_PULL_REQUEST" | grep -oE '[0-9]+$')

if [ -z "$PR_NUMBER" ]; then
    echo "ERROR: Could not extract PR number from $CIRCLE_PULL_REQUEST"
    exit 1
fi

echo "Generating comment for PR #$PR_NUMBER from build #$CIRCLE_BUILD_NUM"

# Get list of artifacts
if [ ! -d "debs" ]; then
    echo "ERROR: debs directory not found"
    exit 1
fi

# Build the comment body
COMMENT_FILE=$(mktemp)

cat > "$COMMENT_FILE" << EOF
## Kernel Build Artifacts

Built from commit \`$(git rev-parse --short HEAD)\` • Build [#${CIRCLE_BUILD_NUM}](https://app.circleci.com/pipelines/github/${CIRCLE_PROJECT_USERNAME}/${CIRCLE_PROJECT_REPONAME}/${CIRCLE_BUILD_NUM})

### Debian Packages

EOF

# List all .deb files with link to artifacts page
ARTIFACTS_URL="https://app.circleci.com/pipelines/github/${CIRCLE_PROJECT_USERNAME}/${CIRCLE_PROJECT_REPONAME}/${CIRCLE_BUILD_NUM}/workflows/${CIRCLE_WORKFLOW_ID}/jobs/${CIRCLE_BUILD_NUM}/artifacts"

for deb_file in debs/*.deb; do
    if [ -f "$deb_file" ]; then
        filename=$(basename "$deb_file")
        echo "- \`$filename\`" >> "$COMMENT_FILE"
    fi
done

echo "" >> "$COMMENT_FILE"
echo "📦 **[Download Artifacts]($ARTIFACTS_URL)**" >> "$COMMENT_FILE"

cat >> "$COMMENT_FILE" << EOF

### Installation

Download the artifacts, then install via ADB:
\`\`\`bash
./install.sh --adb -s <device_serial> install \\
    <path-to-linux-image.deb> \\
    <path-to-linux-modules.deb>
\`\`\`

Or install from local files after building:
\`\`\`bash
./install.sh --adb install
\`\`\`
EOF

echo ""
echo "Comment content:"
echo "================"
cat "$COMMENT_FILE"
echo "================"
echo ""

# Post comment using gh CLI
if ! command -v gh &> /dev/null; then
    echo "ERROR: gh CLI not found. Installing..."
    # For Ubuntu/Debian
    type -p curl >/dev/null || sudo apt install curl -y
    curl -fsSL https://cli.github.com/packages/githubcli-archive-keyring.gpg | sudo dd of=/usr/share/keyrings/githubcli-archive-keyring.gpg
    sudo chmod go+r /usr/share/keyrings/githubcli-archive-keyring.gpg
    echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/githubcli-archive-keyring.gpg] https://cli.github.com/packages stable main" | sudo tee /etc/apt/sources.list.d/github-cli.list > /dev/null
    sudo apt update
    sudo apt install gh -y
fi

# Authenticate gh CLI - try multiple common token variable names
# CircleCI might provide the token as GH_TOKEN, GITHUB_TOKEN, or CIRCLE_TOKEN
if [ -n "$GH_TOKEN" ]; then
    export GH_TOKEN="$GH_TOKEN"
elif [ -n "$GITHUB_TOKEN" ]; then
    export GH_TOKEN="$GITHUB_TOKEN"
elif [ -n "$CIRCLE_TOKEN" ]; then
    export GH_TOKEN="$CIRCLE_TOKEN"
else
    echo "ERROR: No GitHub token found. Set GH_TOKEN, GITHUB_TOKEN, or CIRCLE_TOKEN in CircleCI context."
    exit 1
fi

echo "Posting comment to PR #$PR_NUMBER..."
gh pr comment "$PR_NUMBER" --body-file "$COMMENT_FILE" --repo "$CIRCLE_PROJECT_USERNAME/$CIRCLE_PROJECT_REPONAME"

echo "Comment posted successfully!"

# Cleanup
rm -f "$COMMENT_FILE"
