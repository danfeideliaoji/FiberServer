#!/usr/bin/env bash
set -euo pipefail

BASE_URL="${BASE_URL:-http://localhost:8080}"
DOWNLOAD_BASE_URL="${DOWNLOAD_BASE_URL:-}"
DOWNLOAD_HEADER_BASE_URL="${DOWNLOAD_HEADER_BASE_URL:-$BASE_URL}"
CHUNK_UPLOAD_MODE="${CHUNK_UPLOAD_MODE:-body}"
RUN_ID="e2e$(date +%s)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

require_cmd() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "missing required command: $1" >&2
        exit 1
    }
}

json_code() {
    python3 -c 'import json,sys; print(json.load(sys.stdin).get("code"))'
}

json_field() {
    python3 -c 'import json,sys; print(json.load(sys.stdin).get(sys.argv[1], ""))' "$1"
}

json_array_contains() {
    python3 -c 'import json,sys; data=json.load(sys.stdin); print("yes" if sys.argv[2] in (data.get(sys.argv[1]) or []) else "no")' "$1" "$2"
}

json_artifact_file_id_by_name() {
    python3 -c 'import json,sys; name=sys.argv[1]; data=json.load(sys.stdin); artifacts=data.get("artifacts") or []; print(next((f.get("file_id","") for f in artifacts if f.get("artifact_name") == name), ""))' "$1"
}

json_artifact_field_by_name() {
    python3 -c 'import json,sys; name=sys.argv[1]; field=sys.argv[2]; data=json.load(sys.stdin); artifacts=data.get("artifacts") or []; print(next((f.get(field,"") for f in artifacts if f.get("artifact_name") == name), ""))' "$1" "$2"
}

json_artifact_field() {
    python3 -c 'import json,sys; data=json.load(sys.stdin); print((data.get("artifact") or {}).get(sys.argv[1], ""))' "$1"
}

urlencode() {
    python3 -c 'import sys, urllib.parse; print(urllib.parse.quote(sys.argv[1]))' "$1"
}

assert_code() {
    local name="$1"
    local expected="$2"
    local body="$3"
    local actual
    actual="$(printf '%s' "$body" | json_code)"
    if [[ "$actual" != "$expected" ]]; then
        echo "${name} failed: expected code ${expected}, got ${actual}" >&2
        echo "$body" >&2
        exit 1
    fi
}

assert_http_status() {
    local name="$1"
    local expected="$2"
    local url="$3"
    local actual
    actual="$(curl -sS -o /dev/null -w '%{http_code}' "$url")"
    if [[ "$actual" != "$expected" ]]; then
        echo "${name} failed: expected HTTP ${expected}, got ${actual}" >&2
        exit 1
    fi
}

require_cmd curl
require_cmd python3
require_cmd md5sum
require_cmd cmp
if [[ "$CHUNK_UPLOAD_MODE" != "body" && "$CHUNK_UPLOAD_MODE" != "file" ]]; then
    echo "invalid CHUNK_UPLOAD_MODE: ${CHUNK_UPLOAD_MODE}, expected body or file" >&2
    exit 1
fi

status_response="$(curl -fsS "${BASE_URL}/api/status")"
status_available="$(printf '%s' "$status_response" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("scheduler", {}).get("available"))')"
if [[ "$status_available" != "True" ]]; then
    echo "status failed: scheduler stats unavailable" >&2
    echo "$status_response" >&2
    exit 1
fi

for legacy_path in \
    /api/_/config \
    /api/register \
    /api/login \
    /api/upload \
    /api/upload/dirupload \
    /api/uploadchunk \
    /api/myfiles \
    /api/md5 \
    /api/download \
    /api/deletefile; do
    assert_http_status "legacy route removed ${legacy_path}" "404" "${BASE_URL}${legacy_path}"
done

ARTIFACT_PROJECT="artifact-${RUN_ID}"
ARTIFACT_VERSION="1.0.0"
ARTIFACT_BUILD_NO="build-${RUN_ID}"
ARTIFACT_BRANCH="main"
ARTIFACT_COMMIT_ID="commit-${RUN_ID}"
ARTIFACT_NAME="server-${RUN_ID}.tar.gz"
ARTIFACT_TYPE="application/gzip"
ARTIFACT_CONTENT="fiber artifact e2e ${RUN_ID}"
ARTIFACT_TOKEN="token-${RUN_ID}"
ARTIFACT_CHECKSUM="$(printf '%s' "$ARTIFACT_CONTENT" | md5sum | awk '{print $1}')"
ARTIFACT_SIZE="$(printf '%s' "$ARTIFACT_CONTENT" | wc -c | awk '{print $1}')"
ENCODED_ARTIFACT_NAME="$(urlencode "$ARTIFACT_NAME")"
ENCODED_ARTIFACT_TYPE="$(urlencode "$ARTIFACT_TYPE")"

artifact_token_body="$(printf '{"project_name":"%s","token":"%s"}' "$ARTIFACT_PROJECT" "$ARTIFACT_TOKEN")"
artifact_token_response="$(curl -fsS -H 'Content-Type: application/json' -d "$artifact_token_body" "${BASE_URL}/api/artifacts/token")"
assert_code "artifact token create" "0" "$artifact_token_response"
ARTIFACT_AUTH_HEADER="Authorization: Bearer ${ARTIFACT_TOKEN}"

artifact_precheck_body="$(printf '{"project_name":"%s","checksum":"%s","artifact_name":"%s","version":"%s","build_no":"%s","branch":"%s","commit_id":"%s","size":%s}' "$ARTIFACT_PROJECT" "$ARTIFACT_CHECKSUM" "$ARTIFACT_NAME" "$ARTIFACT_VERSION" "$ARTIFACT_BUILD_NO" "$ARTIFACT_BRANCH" "$ARTIFACT_COMMIT_ID" "$ARTIFACT_SIZE")"
artifact_precheck_response="$(curl -fsS -H 'Content-Type: application/json' -H "$ARTIFACT_AUTH_HEADER" -d "$artifact_precheck_body" "${BASE_URL}/api/artifacts/precheck")"
assert_code "artifact precheck" "1" "$artifact_precheck_response"

artifact_upload_url="${BASE_URL}/api/artifacts/upload/direct?project_name=${ARTIFACT_PROJECT}&checksum=${ARTIFACT_CHECKSUM}&artifact_name=${ENCODED_ARTIFACT_NAME}&version=${ARTIFACT_VERSION}&build_no=${ARTIFACT_BUILD_NO}&branch=${ARTIFACT_BRANCH}&commit_id=${ARTIFACT_COMMIT_ID}&size=${ARTIFACT_SIZE}&artifact_type=${ENCODED_ARTIFACT_TYPE}"
artifact_upload_response="$(printf '%s' "$ARTIFACT_CONTENT" | curl -fsS -H "Content-Type: ${ARTIFACT_TYPE}" -H "$ARTIFACT_AUTH_HEADER" --data-binary @- "$artifact_upload_url")"
assert_code "artifact direct upload" "0" "$artifact_upload_response"

ARTIFACT_CONFLICT_CHECKSUM="$(printf '%s-conflict' "$ARTIFACT_CONTENT" | md5sum | awk '{print $1}')"
artifact_conflict_body="$(printf '{"project_name":"%s","checksum":"%s","artifact_name":"%s","version":"%s","build_no":"%s","branch":"%s","commit_id":"%s","size":%s}' "$ARTIFACT_PROJECT" "$ARTIFACT_CONFLICT_CHECKSUM" "$ARTIFACT_NAME" "$ARTIFACT_VERSION" "$ARTIFACT_BUILD_NO" "$ARTIFACT_BRANCH" "$ARTIFACT_COMMIT_ID" "$ARTIFACT_SIZE")"
artifact_conflict_response="$(curl -fsS -H 'Content-Type: application/json' -H "$ARTIFACT_AUTH_HEADER" -d "$artifact_conflict_body" "${BASE_URL}/api/artifacts/precheck")"
assert_code "artifact coordinate checksum conflict" "3" "$artifact_conflict_response"

artifact_list_body="$(printf '{"project_name":"%s"}' "$ARTIFACT_PROJECT")"
artifact_list_response="$(curl -fsS -H 'Content-Type: application/json' -d "$artifact_list_body" "${BASE_URL}/api/artifacts/list")"
assert_code "artifact list" "0" "$artifact_list_response"
artifact_file_id="$(printf '%s' "$artifact_list_response" | json_artifact_file_id_by_name "$ARTIFACT_NAME")"
if [[ -z "$artifact_file_id" ]]; then
    echo "artifact list failed: uploaded artifact was not listed" >&2
    echo "$artifact_list_response" >&2
    exit 1
fi
artifact_branch="$(printf '%s' "$artifact_list_response" | json_artifact_field_by_name "$ARTIFACT_NAME" branch)"
artifact_commit_id="$(printf '%s' "$artifact_list_response" | json_artifact_field_by_name "$ARTIFACT_NAME" commit_id)"
if [[ "$artifact_branch" != "$ARTIFACT_BRANCH" || "$artifact_commit_id" != "$ARTIFACT_COMMIT_ID" ]]; then
    echo "artifact list failed: branch/commit_id metadata mismatch" >&2
    echo "$artifact_list_response" >&2
    exit 1
fi

artifact_headers="$(curl -fsSI "${DOWNLOAD_HEADER_BASE_URL}/api/artifacts/download?project_name=${ARTIFACT_PROJECT}&artifact_name=${ENCODED_ARTIFACT_NAME}&version=${ARTIFACT_VERSION}&build_no=${ARTIFACT_BUILD_NO}")"
if ! printf '%s\n' "$artifact_headers" | grep -qi '^X-Accel-Redirect:'; then
    echo "artifact download failed: missing X-Accel-Redirect header" >&2
    printf '%s\n' "$artifact_headers" >&2
    exit 1
fi

if [[ -n "$DOWNLOAD_BASE_URL" ]]; then
    artifact_downloaded="$(curl -fsS "${DOWNLOAD_BASE_URL}/api/artifacts/download?project_name=${ARTIFACT_PROJECT}&artifact_name=${ENCODED_ARTIFACT_NAME}&version=${ARTIFACT_VERSION}&build_no=${ARTIFACT_BUILD_NO}")"
    if [[ "$artifact_downloaded" != "$ARTIFACT_CONTENT" ]]; then
        echo "artifact download failed: content mismatch through ${DOWNLOAD_BASE_URL}" >&2
        exit 1
    fi
fi

artifact_latest_response="$(curl -fsS "${BASE_URL}/api/artifacts/latest?project_name=${ARTIFACT_PROJECT}")"
assert_code "artifact latest" "0" "$artifact_latest_response"
latest_artifact_name="$(printf '%s' "$artifact_latest_response" | json_artifact_field artifact_name)"
if [[ "$latest_artifact_name" != "$ARTIFACT_NAME" ]]; then
    echo "artifact latest failed: expected ${ARTIFACT_NAME}, got ${latest_artifact_name}" >&2
    echo "$artifact_latest_response" >&2
    exit 1
fi

artifact_versions_response="$(curl -fsS "${BASE_URL}/api/artifacts/versions?project_name=${ARTIFACT_PROJECT}")"
assert_code "artifact versions" "0" "$artifact_versions_response"
if [[ "$(printf '%s' "$artifact_versions_response" | json_array_contains versions "$ARTIFACT_VERSION")" != "yes" ]]; then
    echo "artifact versions failed: missing ${ARTIFACT_VERSION}" >&2
    echo "$artifact_versions_response" >&2
    exit 1
fi

artifact_builds_response="$(curl -fsS "${BASE_URL}/api/artifacts/builds?project_name=${ARTIFACT_PROJECT}&version=${ARTIFACT_VERSION}")"
assert_code "artifact builds" "0" "$artifact_builds_response"
if [[ "$(printf '%s' "$artifact_builds_response" | json_array_contains builds "$ARTIFACT_BUILD_NO")" != "yes" ]]; then
    echo "artifact builds failed: missing ${ARTIFACT_BUILD_NO}" >&2
    echo "$artifact_builds_response" >&2
    exit 1
fi

artifact_delete_body="$(printf '{"project_name":"%s","artifact_name":"%s","version":"%s","build_no":"%s"}' "$ARTIFACT_PROJECT" "$ARTIFACT_NAME" "$ARTIFACT_VERSION" "$ARTIFACT_BUILD_NO")"
artifact_delete_response="$(curl -fsS -H 'Content-Type: application/json' -H "$ARTIFACT_AUTH_HEADER" -d "$artifact_delete_body" "${BASE_URL}/api/artifacts/delete")"
assert_code "artifact delete" "0" "$artifact_delete_response"

CHUNK_VERSION="2.0.0"
CHUNK_BUILD_NO="chunk-${RUN_ID}"
CHUNK_ARTIFACT_NAME="server-chunk-${RUN_ID}.bin"
CHUNK_ARTIFACT_TYPE="application/octet-stream"
CHUNK_FILE="${TMP_DIR}/${CHUNK_ARTIFACT_NAME}"
python3 -c 'from pathlib import Path; import sys; size = 6 * 1024 * 1024 + 123; pattern = f"FiberServer artifact chunk e2e {sys.argv[2]}\n".encode(); Path(sys.argv[1]).write_bytes((pattern * (size // len(pattern) + 1))[:size])' "$CHUNK_FILE" "$RUN_ID"
CHUNK_CHECKSUM="$(md5sum "$CHUNK_FILE" | awk '{print $1}')"
CHUNK_SIZE="$(wc -c < "$CHUNK_FILE" | awk '{print $1}')"
ENCODED_CHUNK_ARTIFACT_NAME="$(urlencode "$CHUNK_ARTIFACT_NAME")"
ENCODED_CHUNK_ARTIFACT_TYPE="$(urlencode "$CHUNK_ARTIFACT_TYPE")"

chunk_precheck_body="$(printf '{"project_name":"%s","checksum":"%s","artifact_name":"%s","version":"%s","build_no":"%s","size":%s,"artifact_type":"%s"}' "$ARTIFACT_PROJECT" "$CHUNK_CHECKSUM" "$CHUNK_ARTIFACT_NAME" "$CHUNK_VERSION" "$CHUNK_BUILD_NO" "$CHUNK_SIZE" "$CHUNK_ARTIFACT_TYPE")"
chunk_precheck_response="$(curl -fsS -H 'Content-Type: application/json' -H "$ARTIFACT_AUTH_HEADER" -d "$chunk_precheck_body" "${BASE_URL}/api/artifacts/precheck")"
assert_code "artifact chunk precheck" "2" "$chunk_precheck_response"
total_chunks="$(printf '%s' "$chunk_precheck_response" | json_field totalChunks)"
if [[ -z "$total_chunks" || "$total_chunks" -le 0 ]]; then
    echo "artifact chunk precheck failed: missing totalChunks" >&2
    echo "$chunk_precheck_response" >&2
    exit 1
fi

for ((i = 0; i < total_chunks; ++i)); do
    chunk_tmp="${TMP_DIR}/${ARTIFACT_PROJECT}-${CHUNK_CHECKSUM}-${i}.part"
    python3 -c 'from pathlib import Path; import sys; src=Path(sys.argv[1]); out=Path(sys.argv[2]); idx=int(sys.argv[3]); total=int(sys.argv[4]); data=src.read_bytes(); step=(len(data)+total-1)//total; out.write_bytes(data[idx*step:min((idx+1)*step, len(data))])' "$CHUNK_FILE" "$chunk_tmp" "$i" "$total_chunks"
    chunk_url="${BASE_URL}/api/artifacts/upload/chunk?project_name=${ARTIFACT_PROJECT}&checksum=${CHUNK_CHECKSUM}&artifact_name=${ENCODED_CHUNK_ARTIFACT_NAME}&version=${CHUNK_VERSION}&build_no=${CHUNK_BUILD_NO}&size=${CHUNK_SIZE}&artifact_type=${ENCODED_CHUNK_ARTIFACT_TYPE}&total_chunks=${total_chunks}&chunk_index=${i}"
    chunk_response="$(curl -fsS -X POST -H "$ARTIFACT_AUTH_HEADER" -H "Content-Type: ${CHUNK_ARTIFACT_TYPE}" --data-binary "@${chunk_tmp}" "$chunk_url")"
    expected_code="0"
    if [[ "$i" -eq $((total_chunks - 1)) ]]; then
        expected_code="2"
    fi
    assert_code "artifact chunk upload ${i}" "$expected_code" "$chunk_response"
done

chunk_list_response="$(curl -fsS -H 'Content-Type: application/json' -d "$artifact_list_body" "${BASE_URL}/api/artifacts/list")"
assert_code "artifact list after chunk upload" "0" "$chunk_list_response"
chunk_file_id="$(printf '%s' "$chunk_list_response" | json_artifact_file_id_by_name "$CHUNK_ARTIFACT_NAME")"
if [[ -z "$chunk_file_id" ]]; then
    echo "artifact list failed: chunk artifact was not listed" >&2
    echo "$chunk_list_response" >&2
    exit 1
fi

if [[ -n "$DOWNLOAD_BASE_URL" ]]; then
    downloaded_chunk="${TMP_DIR}/downloaded-${CHUNK_ARTIFACT_NAME}"
    curl -fsS "${DOWNLOAD_BASE_URL}/api/artifacts/download?project_name=${ARTIFACT_PROJECT}&artifact_name=${ENCODED_CHUNK_ARTIFACT_NAME}&version=${CHUNK_VERSION}&build_no=${CHUNK_BUILD_NO}" -o "$downloaded_chunk"
    if ! cmp -s "$CHUNK_FILE" "$downloaded_chunk"; then
        echo "artifact chunk download failed: content mismatch through ${DOWNLOAD_BASE_URL}" >&2
        exit 1
    fi
fi

chunk_delete_body="$(printf '{"project_name":"%s","artifact_name":"%s","version":"%s","build_no":"%s"}' "$ARTIFACT_PROJECT" "$CHUNK_ARTIFACT_NAME" "$CHUNK_VERSION" "$CHUNK_BUILD_NO")"
chunk_delete_response="$(curl -fsS -H 'Content-Type: application/json' -H "$ARTIFACT_AUTH_HEADER" -d "$chunk_delete_body" "${BASE_URL}/api/artifacts/delete")"
assert_code "artifact chunk delete" "0" "$chunk_delete_response"

echo "e2e passed: project=${ARTIFACT_PROJECT} direct_file_id=${artifact_file_id} chunk_file_id=${chunk_file_id} chunk_mode=${CHUNK_UPLOAD_MODE}"
