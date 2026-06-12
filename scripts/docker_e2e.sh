#!/usr/bin/env bash
set -euo pipefail

BASE_URL="${BASE_URL:-http://localhost:8080}"
DOWNLOAD_BASE_URL="${DOWNLOAD_BASE_URL:-}"
SHARED_TMP_ROOT="${SHARED_TMP_ROOT:-/var/data/tmp_uploads}"
USER_NAME="e2e$(date +%s)"
PASSWORD="pass123"
CONTENT="fiber docker e2e ${USER_NAME}"
FILE_NAME="sample-${USER_NAME}.txt"
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
mkdir -p "$SHARED_TMP_ROOT"

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
ENCODED_TYPE="$(urlencode "$FILE_TYPE")"

register_body="$(printf '{"username":"%s","password":"%s","nickname":"%s"}' "$USER_NAME" "$PASSWORD" "$USER_NAME")"
register_response="$(curl -fsS -H 'Content-Type: application/json' -d "$register_body" "${BASE_URL}/api/register")"
assert_code "register" "0" "$register_response"

login_body="$(printf '{"user":"%s","pwd":"%s"}' "$USER_NAME" "$PASSWORD")"
login_response="$(curl -fsS -H 'Content-Type: application/json' -d "$login_body" "${BASE_URL}/api/login")"
assert_code "login" "0" "$login_response"

precheck_body="$(printf '{"username":"%s","md5":"%s","filename":"%s","size":%s}' "$USER_NAME" "$MD5" "$FILE_NAME" "$SIZE")"
precheck_response="$(curl -fsS -H 'Content-Type: application/json' -d "$precheck_body" "${BASE_URL}/api/upload")"
assert_code "upload precheck" "1" "$precheck_response"

upload_url="${BASE_URL}/api/upload/dirupload?username=${USER_NAME}&md5=${MD5}&filename=${ENCODED_FILE}&size=${SIZE}&type=${ENCODED_TYPE}"
upload_response="$(printf '%s' "$CONTENT" | curl -fsS -H "Content-Type: ${FILE_TYPE}" --data-binary @- "$upload_url")"
assert_code "direct upload" "0" "$upload_response"

files_body="$(printf '{"username":"%s"}' "$USER_NAME")"
files_response="$(curl -fsS -H 'Content-Type: application/json' -d "$files_body" "${BASE_URL}/api/myfiles")"
assert_code "myfiles" "0" "$files_response"

file_id="$(printf '%s' "$files_response" | json_first_file_id)"
if [[ -z "$file_id" ]]; then
    echo "myfiles failed: uploaded file was not listed" >&2
    echo "$files_response" >&2
    exit 1
fi

headers="$(curl -fsSI "${BASE_URL}/api/download?user=${USER_NAME}&filename=${ENCODED_FILE}")"
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

CHUNK_FILE="${TMP_DIR}/${CHUNK_FILE_NAME}"
python3 -c 'from pathlib import Path; import sys; size = 6 * 1024 * 1024 + 123; pattern = b"FiberServer chunk e2e\n"; Path(sys.argv[1]).write_bytes((pattern * (size // len(pattern) + 1))[:size])' "$CHUNK_FILE"
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
    chunk_tmp="${SHARED_TMP_ROOT}/${USER_NAME}-${CHUNK_MD5}-${i}.part"
    python3 -c 'from pathlib import Path; import sys; src=Path(sys.argv[1]); out=Path(sys.argv[2]); idx=int(sys.argv[3]); total=int(sys.argv[4]); data=src.read_bytes(); step=(len(data)+total-1)//total; out.write_bytes(data[idx*step:min((idx+1)*step, len(data))])' "$CHUNK_FILE" "$chunk_tmp" "$i" "$total_chunks"
    chunk_url="${BASE_URL}/api/uploadchunk?username=${USER_NAME}&md5=${CHUNK_MD5}&filename=${ENCODED_CHUNK_FILE}&size=${CHUNK_SIZE}&type=${ENCODED_CHUNK_TYPE}&total_chunks=${total_chunks}&chunk_index=${i}"
    chunk_response="$(curl -fsS -X POST -H "X-File-Path: ${chunk_tmp}" "$chunk_url")"
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

echo "e2e passed: user=${USER_NAME} file_id=${file_id} chunk_file_id=${chunk_file_id}"
