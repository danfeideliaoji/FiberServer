#!/usr/bin/env bash
set -euo pipefail

BASE_URL="${BASE_URL:-http://localhost:8080}"
DOWNLOAD_BASE_URL="${DOWNLOAD_BASE_URL:-}"
DOWNLOAD_HEADER_BASE_URL="${DOWNLOAD_HEADER_BASE_URL:-$BASE_URL}"
SHARED_TMP_ROOT="${SHARED_TMP_ROOT:-/var/data/tmp_uploads}"
CHUNK_UPLOAD_MODE="${CHUNK_UPLOAD_MODE:-body}"
USER_NAME="e2e$(date +%s)"
SECOND_USER_NAME="${USER_NAME}b"
PASSWORD="pass123"
CONTENT="fiber docker e2e ${USER_NAME}"
FILE_NAME="sample-${USER_NAME}.txt"
INSTANT_FILE_NAME="instant-${USER_NAME}.txt"
FILE_TYPE="text/plain"
CHUNK_FILE_NAME="chunk-${USER_NAME}.bin"
CHUNK_FILE_TYPE="application/octet-stream"
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

json_first_file_id() {
    python3 -c 'import json,sys; data=json.load(sys.stdin); files=data.get("files") or []; print(files[0].get("file_id") if files else "")'
}

json_file_id_by_name() {
    python3 -c 'import json,sys; name=sys.argv[1]; data=json.load(sys.stdin); files=data.get("files") or []; print(next((f.get("file_id","") for f in files if f.get("filename") == name), ""))' "$1"
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

json_array_contains() {
    python3 -c 'import json,sys; data=json.load(sys.stdin); print("yes" if sys.argv[2] in (data.get(sys.argv[1]) or []) else "no")' "$1" "$2"
}

json_field() {
    python3 -c 'import json,sys; print(json.load(sys.stdin).get(sys.argv[1], ""))' "$1"
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

require_cmd curl
require_cmd python3
require_cmd md5sum
require_cmd cmp
if [[ "$CHUNK_UPLOAD_MODE" != "body" && "$CHUNK_UPLOAD_MODE" != "file" ]]; then
    echo "invalid CHUNK_UPLOAD_MODE: ${CHUNK_UPLOAD_MODE}, expected body or file" >&2
    exit 1
fi
if [[ "$CHUNK_UPLOAD_MODE" == "file" ]]; then
    mkdir -p "$SHARED_TMP_ROOT"
fi

status_response="$(curl -fsS "${BASE_URL}/api/status")"
status_available="$(printf '%s' "$status_response" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("scheduler", {}).get("available"))')"
if [[ "$status_available" != "True" ]]; then
    echo "status failed: scheduler stats unavailable" >&2
    echo "$status_response" >&2
    exit 1
fi

MD5="$(printf '%s' "$CONTENT" | md5sum | awk '{print $1}')"
SIZE="$(printf '%s' "$CONTENT" | wc -c | awk '{print $1}')"
ENCODED_FILE="$(urlencode "$FILE_NAME")"
ENCODED_INSTANT_FILE="$(urlencode "$INSTANT_FILE_NAME")"
ENCODED_TYPE="$(urlencode "$FILE_TYPE")"

register_body="$(printf '{"username":"%s","password":"%s","nickname":"%s"}' "$USER_NAME" "$PASSWORD" "$USER_NAME")"
register_response="$(curl -fsS -H 'Content-Type: application/json' -d "$register_body" "${BASE_URL}/api/register")"
assert_code "register" "0" "$register_response"

duplicate_register_response="$(curl -fsS -H 'Content-Type: application/json' -d "$register_body" "${BASE_URL}/api/register")"
assert_code "duplicate register" "2" "$duplicate_register_response"

wrong_login_body="$(printf '{"user":"%s","pwd":"wrong-password"}' "$USER_NAME")"
wrong_login_response="$(curl -fsS -H 'Content-Type: application/json' -d "$wrong_login_body" "${BASE_URL}/api/login")"
assert_code "wrong password login" "2" "$wrong_login_response"

login_body="$(printf '{"user":"%s","pwd":"%s"}' "$USER_NAME" "$PASSWORD")"
login_response="$(curl -fsS -H 'Content-Type: application/json' -d "$login_body" "${BASE_URL}/api/login")"
assert_code "login" "0" "$login_response"

precheck_body="$(printf '{"username":"%s","md5":"%s","filename":"%s","size":%s}' "$USER_NAME" "$MD5" "$FILE_NAME" "$SIZE")"
precheck_response="$(curl -fsS -H 'Content-Type: application/json' -d "$precheck_body" "${BASE_URL}/api/upload")"
assert_code "upload precheck" "1" "$precheck_response"

upload_url="${BASE_URL}/api/upload/dirupload?username=${USER_NAME}&md5=${MD5}&filename=${ENCODED_FILE}&size=${SIZE}&type=${ENCODED_TYPE}"
upload_response="$(printf '%s' "$CONTENT" | curl -fsS -H "Content-Type: ${FILE_TYPE}" --data-binary @- "$upload_url")"
assert_code "direct upload" "0" "$upload_response"

md5_body="$(printf '{"username":"%s","md5":"%s","filename":"%s"}' "$USER_NAME" "$MD5" "$FILE_NAME")"
md5_response="$(curl -fsS -H 'Content-Type: application/json' -d "$md5_body" "${BASE_URL}/api/md5")"
assert_code "md5 instant check" "0" "$md5_response"

second_register_body="$(printf '{"username":"%s","password":"%s","nickname":"%s"}' "$SECOND_USER_NAME" "$PASSWORD" "$SECOND_USER_NAME")"
second_register_response="$(curl -fsS -H 'Content-Type: application/json' -d "$second_register_body" "${BASE_URL}/api/register")"
assert_code "second register" "0" "$second_register_response"

second_login_body="$(printf '{"user":"%s","pwd":"%s"}' "$SECOND_USER_NAME" "$PASSWORD")"
second_login_response="$(curl -fsS -H 'Content-Type: application/json' -d "$second_login_body" "${BASE_URL}/api/login")"
assert_code "second login" "0" "$second_login_response"

instant_precheck_body="$(printf '{"username":"%s","md5":"%s","filename":"%s","size":%s}' "$SECOND_USER_NAME" "$MD5" "$INSTANT_FILE_NAME" "$SIZE")"
instant_precheck_response="$(curl -fsS -H 'Content-Type: application/json' -d "$instant_precheck_body" "${BASE_URL}/api/upload")"
assert_code "instant upload precheck" "0" "$instant_precheck_response"

files_body="$(printf '{"username":"%s"}' "$USER_NAME")"
files_response="$(curl -fsS -H 'Content-Type: application/json' -d "$files_body" "${BASE_URL}/api/myfiles")"
assert_code "myfiles" "0" "$files_response"

file_id="$(printf '%s' "$files_response" | json_first_file_id)"
if [[ -z "$file_id" ]]; then
    echo "myfiles failed: uploaded file was not listed" >&2
    echo "$files_response" >&2
    exit 1
fi

headers="$(curl -fsSI "${DOWNLOAD_HEADER_BASE_URL}/api/download?user=${USER_NAME}&filename=${ENCODED_FILE}")"
if ! printf '%s\n' "$headers" | grep -qi '^X-Accel-Redirect:'; then
    echo "download failed: missing X-Accel-Redirect header" >&2
    printf '%s\n' "$headers" >&2
    exit 1
fi

if [[ -n "$DOWNLOAD_BASE_URL" ]]; then
    downloaded="$(curl -fsS "${DOWNLOAD_BASE_URL}/api/download?user=${USER_NAME}&filename=${ENCODED_FILE}")"
    if [[ "$downloaded" != "$CONTENT" ]]; then
        echo "download failed: content mismatch through ${DOWNLOAD_BASE_URL}" >&2
        echo "expected: $CONTENT" >&2
        echo "actual:   $downloaded" >&2
        exit 1
    fi
fi

second_files_body="$(printf '{"username":"%s"}' "$SECOND_USER_NAME")"
second_files_response="$(curl -fsS -H 'Content-Type: application/json' -d "$second_files_body" "${BASE_URL}/api/myfiles")"
assert_code "second myfiles" "0" "$second_files_response"
instant_file_id="$(printf '%s' "$second_files_response" | json_file_id_by_name "$INSTANT_FILE_NAME")"
if [[ -z "$instant_file_id" ]]; then
    echo "second myfiles failed: instant uploaded file was not listed" >&2
    echo "$second_files_response" >&2
    exit 1
fi

if [[ -n "$DOWNLOAD_BASE_URL" ]]; then
    instant_downloaded="$(curl -fsS "${DOWNLOAD_BASE_URL}/api/download?user=${SECOND_USER_NAME}&filename=${ENCODED_INSTANT_FILE}")"
    if [[ "$instant_downloaded" != "$CONTENT" ]]; then
        echo "instant download failed: content mismatch through ${DOWNLOAD_BASE_URL}" >&2
        exit 1
    fi
fi

delete_body="$(printf '{"user":"%s","file_name":"%s"}' "$USER_NAME" "$FILE_NAME")"
delete_response="$(curl -fsS -H 'Content-Type: application/json' -d "$delete_body" "${BASE_URL}/api/deletefile")"
assert_code "delete original file" "0" "$delete_response"

if [[ -n "$DOWNLOAD_BASE_URL" ]]; then
    instant_after_delete="$(curl -fsS "${DOWNLOAD_BASE_URL}/api/download?user=${SECOND_USER_NAME}&filename=${ENCODED_INSTANT_FILE}")"
    if [[ "$instant_after_delete" != "$CONTENT" ]]; then
        echo "instant download after original delete failed: content mismatch through ${DOWNLOAD_BASE_URL}" >&2
        exit 1
    fi
fi

delete_instant_body="$(printf '{"user":"%s","file_name":"%s"}' "$SECOND_USER_NAME" "$INSTANT_FILE_NAME")"
delete_instant_response="$(curl -fsS -H 'Content-Type: application/json' -d "$delete_instant_body" "${BASE_URL}/api/deletefile")"
assert_code "delete instant file" "0" "$delete_instant_response"

ARTIFACT_PROJECT="artifact-${USER_NAME}"
ARTIFACT_VERSION="1.0.0"
ARTIFACT_BUILD_NO="build-${USER_NAME}"
ARTIFACT_BRANCH="main"
ARTIFACT_COMMIT_ID="commit-${USER_NAME}"
ARTIFACT_NAME="server-${USER_NAME}.tar.gz"
ARTIFACT_TYPE="application/gzip"
ARTIFACT_CONTENT="fiber artifact e2e ${USER_NAME}"
ARTIFACT_TOKEN="token-${USER_NAME}"
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

CHUNK_FILE="${TMP_DIR}/${CHUNK_FILE_NAME}"
python3 -c 'from pathlib import Path; import sys; size = 6 * 1024 * 1024 + 123; pattern = f"FiberServer chunk e2e {sys.argv[2]}\n".encode(); Path(sys.argv[1]).write_bytes((pattern * (size // len(pattern) + 1))[:size])' "$CHUNK_FILE" "$USER_NAME"
CHUNK_MD5="$(md5sum "$CHUNK_FILE" | awk '{print $1}')"
CHUNK_SIZE="$(wc -c < "$CHUNK_FILE" | awk '{print $1}')"
ENCODED_CHUNK_FILE="$(urlencode "$CHUNK_FILE_NAME")"
ENCODED_CHUNK_TYPE="$(urlencode "$CHUNK_FILE_TYPE")"

chunk_precheck_body="$(printf '{"username":"%s","md5":"%s","filename":"%s","size":%s}' "$USER_NAME" "$CHUNK_MD5" "$CHUNK_FILE_NAME" "$CHUNK_SIZE")"
chunk_precheck_response="$(curl -fsS -H 'Content-Type: application/json' -d "$chunk_precheck_body" "${BASE_URL}/api/upload")"
assert_code "chunk upload precheck" "2" "$chunk_precheck_response"
total_chunks="$(printf '%s' "$chunk_precheck_response" | json_field totalChunks)"
if [[ -z "$total_chunks" || "$total_chunks" -le 0 ]]; then
    echo "chunk upload precheck failed: missing totalChunks" >&2
    echo "$chunk_precheck_response" >&2
    exit 1
fi

for ((i = 0; i < total_chunks; ++i)); do
    if [[ "$CHUNK_UPLOAD_MODE" == "file" ]]; then
        chunk_tmp="${SHARED_TMP_ROOT}/${USER_NAME}-${CHUNK_MD5}-${i}.part"
    else
        chunk_tmp="${TMP_DIR}/${USER_NAME}-${CHUNK_MD5}-${i}.part"
    fi
    python3 -c 'from pathlib import Path; import sys; src=Path(sys.argv[1]); out=Path(sys.argv[2]); idx=int(sys.argv[3]); total=int(sys.argv[4]); data=src.read_bytes(); step=(len(data)+total-1)//total; out.write_bytes(data[idx*step:min((idx+1)*step, len(data))])' "$CHUNK_FILE" "$chunk_tmp" "$i" "$total_chunks"
    chunk_url="${BASE_URL}/api/uploadchunk?username=${USER_NAME}&md5=${CHUNK_MD5}&filename=${ENCODED_CHUNK_FILE}&size=${CHUNK_SIZE}&type=${ENCODED_CHUNK_TYPE}&total_chunks=${total_chunks}&chunk_index=${i}"
    if [[ "$CHUNK_UPLOAD_MODE" == "file" ]]; then
        chunk_response="$(curl -fsS -X POST -H "X-File-Path: ${chunk_tmp}" "$chunk_url")"
    else
        chunk_response="$(curl -fsS -X POST -H "Content-Type: ${CHUNK_FILE_TYPE}" --data-binary "@${chunk_tmp}" "$chunk_url")"
    fi
    expected_code="0"
    if [[ "$i" -eq $((total_chunks - 1)) ]]; then
        expected_code="2"
    fi
    assert_code "chunk upload ${i}" "$expected_code" "$chunk_response"
done

chunk_files_response="$(curl -fsS -H 'Content-Type: application/json' -d "$files_body" "${BASE_URL}/api/myfiles")"
assert_code "myfiles after chunk upload" "0" "$chunk_files_response"
chunk_file_id="$(printf '%s' "$chunk_files_response" | json_file_id_by_name "$CHUNK_FILE_NAME")"
if [[ -z "$chunk_file_id" ]]; then
    echo "myfiles failed: chunk uploaded file was not listed" >&2
    echo "$chunk_files_response" >&2
    exit 1
fi

if [[ -n "$DOWNLOAD_BASE_URL" ]]; then
    downloaded_chunk="${TMP_DIR}/downloaded-${CHUNK_FILE_NAME}"
    curl -fsS "${DOWNLOAD_BASE_URL}/api/download?user=${USER_NAME}&filename=${ENCODED_CHUNK_FILE}" -o "$downloaded_chunk"
    if ! cmp -s "$CHUNK_FILE" "$downloaded_chunk"; then
        echo "chunk download failed: content mismatch through ${DOWNLOAD_BASE_URL}" >&2
        exit 1
    fi
fi

delete_chunk_body="$(printf '{"user":"%s","file_name":"%s"}' "$USER_NAME" "$CHUNK_FILE_NAME")"
delete_chunk_response="$(curl -fsS -H 'Content-Type: application/json' -d "$delete_chunk_body" "${BASE_URL}/api/deletefile")"
assert_code "delete chunk file" "0" "$delete_chunk_response"

echo "e2e passed: user=${USER_NAME} second_user=${SECOND_USER_NAME} file_id=${file_id} instant_file_id=${instant_file_id} chunk_file_id=${chunk_file_id} chunk_mode=${CHUNK_UPLOAD_MODE}"
