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

if [ -z "$CIRCLE_TOKEN" ]; then
    echo "ERROR: CIRCLE_TOKEN not set (required for API access)"
    exit 1
fi

# Install jq if not available
if ! command -v jq &> /dev/null; then
    echo "Installing jq..."
    sudo apt-get update -qq
    sudo apt-get install -y jq
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

Built from commit \`$(git rev-parse --short HEAD)\`

### Debian Packages

EOF

# Get artifact URLs from CircleCI API
# We need to use the workflow job ID, which requires an API call
echo "Fetching artifact URLs from CircleCI API..."

# Get workflow jobs to find the current job ID
WORKFLOW_JOBS=$(curl -s "https://circleci.com/api/v2/workflow/${CIRCLE_WORKFLOW_ID}/job" -H "Circle-Token: ${CIRCLE_TOKEN}")
BUILD_JOB_ID=$(echo "$WORKFLOW_JOBS" | jq -r '.items[] | select(.name == "build-kernel") | .job_number')

if [ -z "$BUILD_JOB_ID" ]; then
    echo "ERROR: Could not find build-kernel job ID"
    exit 1
fi

echo "Found build-kernel job ID: $BUILD_JOB_ID"

# Get artifacts for the build job
ARTIFACTS_JSON=$(curl -s "https://circleci.com/api/v2/project/gh/${CIRCLE_PROJECT_USERNAME}/${CIRCLE_PROJECT_REPONAME}/${BUILD_JOB_ID}/artifacts" -H "Circle-Token: ${CIRCLE_TOKEN}")

# Add links for each .deb file
for deb_file in debs/*.deb; do
    if [ -f "$deb_file" ]; then
        filename=$(basename "$deb_file")

        # Extract the artifact URL from the API response
        artifact_url=$(echo "$ARTIFACTS_JSON" | jq -r ".items[] | select(.path | endswith(\"$filename\")) | .url")

        if [ -n "$artifact_url" ] && [ "$artifact_url" != "null" ]; then
            echo "- [\`$filename\`]($artifact_url)" >> "$COMMENT_FILE"
        else
            # Fallback to generic artifacts page if URL not found
            echo "- \`$filename\` - [View Artifacts](https://app.circleci.com/pipelines/github/${CIRCLE_PROJECT_USERNAME}/${CIRCLE_PROJECT_REPONAME}/${BUILD_JOB_ID}/tests#artifacts)" >> "$COMMENT_FILE"
        fi
    fi
done

cat >> "$COMMENT_FILE" << EOF

### Installation

Install via ADB:
\`\`\`bash
./install.sh --adb -s <device_serial> install \\
EOF

# Add the first two packages (image and modules) to the install example
first_image=$(ls debs/linux-image-*.deb 2>/dev/null | head -1)
first_modules=$(ls debs/linux-modules-*.deb 2>/dev/null | head -1)

if [ -n "$first_image" ]; then
    filename=$(basename "$first_image")
    image_url=$(echo "$ARTIFACTS_JSON" | jq -r ".items[] | select(.path | endswith(\"$filename\")) | .url")
    if [ -n "$image_url" ] && [ "$image_url" != "null" ]; then
        echo "    \"$image_url\" \\" >> "$COMMENT_FILE"
    fi
fi

if [ -n "$first_modules" ]; then
    filename=$(basename "$first_modules")
    modules_url=$(echo "$ARTIFACTS_JSON" | jq -r ".items[] | select(.path | endswith(\"$filename\")) | .url")
    if [ -n "$modules_url" ] && [ "$modules_url" != "null" ]; then
        echo "    \"$modules_url\"" >> "$COMMENT_FILE"
    fi
fi

cat >> "$COMMENT_FILE" << EOF
\`\`\`

Or install from local files after building:
\`\`\`bash
./install.sh --adb install
\`\`\`

---
**Build**: [#${BUILD_JOB_ID}](https://app.circleci.com/pipelines/github/${CIRCLE_PROJECT_USERNAME}/${CIRCLE_PROJECT_REPONAME}/${BUILD_JOB_ID})
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

# Authenticate gh CLI (assumes GH_TOKEN is set in environment)
if [ -z "$GH_TOKEN" ]; then
    echo "ERROR: GH_TOKEN not set"
    exit 1
fi

export GH_TOKEN="$GH_TOKEN"

echo "Posting comment to PR #$PR_NUMBER..."
gh pr comment "$PR_NUMBER" --body-file "$COMMENT_FILE" --repo "$CIRCLE_PROJECT_USERNAME/$CIRCLE_PROJECT_REPONAME"

echo "Comment posted successfully!"

# Cleanup
rm -f "$COMMENT_FILE"
