#!/usr/bin/env bash
# Jenkins 用 withCredentials 注入 SMB_USERNAME/SMB_PASSWORD；不需要挂载共享。
# check: 检查连接；publish: 上传 target/ 下的包、SHA256、版本文件。
set +x
set -euo pipefail
export LC_ALL=C
umask 077

: "${SMB_SHARE:?未设置 SMB_SHARE}"
: "${SMB_USERNAME:?未绑定 SMB 用户名凭据}"
: "${SMB_PASSWORD:?未绑定 SMB 密码凭据}"
SMB_DOMAIN=${SMB_DOMAIN:-}
mode=${1:-publish}
case "$mode" in check|publish) ;; *) echo 'Usage: publish-smb.sh check|publish' >&2; exit 1;; esac
command -v smbclient >/dev/null || { echo '缺少 smbclient，请更新 Jenkins 构建镜像' >&2; exit 1; }
# auth 文件采用逐行格式；拒绝换行，且错误信息不包含凭据内容。
for value in "$SMB_USERNAME" "$SMB_PASSWORD" "$SMB_DOMAIN"; do
    case "$value" in *$'\n'*|*$'\r'*) echo 'SMB 凭据和域名不能包含换行' >&2; exit 1;; esac
done
unset value

# 放在容器 /tmp 而非 Jenkins workspace，权限 0600，退出时清理。
private_dir=$(mktemp -d /tmp/jenkins-smb.XXXXXXXX)
auth_file="$private_dir/auth"
remote_temporary=".upload-${private_dir##*/}"
remote_created=0
smb() {
    # 一次调用只执行一条命令，避免后续命令成功掩盖前面的上传失败。
    smbclient "$SMB_SHARE" -A "$auth_file" -p 445 -m SMB3 \
        --option='client min protocol=SMB2' -t 60 -c "$1"
}
cleanup() {
    local status=$?
    trap - EXIT
    set +e
    if [[ "$remote_created" == 1 ]]; then
        # 只删除本次创建的临时目录中的三个文件，绝不清理已发布的版本。
        for file in "$ARCHIVE_NAME" "$ARCHIVE_NAME.sha256" "$ARCHIVE_NAME.version.txt"; do
            smb "del \"$remote_temporary/$file\"" >/dev/null 2>&1
        done
        if ! smb "rmdir \"$remote_temporary\"" >/dev/null 2>&1; then
            echo "SMB_PUBLISH cleanup_failed: $SMB_SHARE/$remote_temporary" >&2
        fi
    fi
    rm -rf -- "$private_dir"
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
{
    printf 'username = %s\n' "$SMB_USERNAME"
    printf 'password = %s\n' "$SMB_PASSWORD"
    [[ -z "$SMB_DOMAIN" ]] || printf 'domain = %s\n' "$SMB_DOMAIN"
    :
} > "$auth_file"
unset SMB_USERNAME SMB_PASSWORD

if [[ "$mode" == check ]]; then
    smb 'ls' >/dev/null
    echo "SMB_CHECK success: $SMB_SHARE"
    exit 0
fi

: "${RELEASE_VERSION:?未设置 RELEASE_VERSION}"
: "${ARCHIVE_NAME:?未设置 ARCHIVE_NAME}"
# 文件名还会送入 smbclient 命令解释器，因此只允许单个安全路径组件。
[[ "$RELEASE_VERSION" =~ ^[A-Za-z0-9][A-Za-z0-9_.-]*$ && "$ARCHIVE_NAME" == "$RELEASE_VERSION.tar.gz" ]] || {
    echo '版本或安装包文件名不合法' >&2; exit 1;
}
cd target
for file in "$ARCHIVE_NAME" "$ARCHIVE_NAME.sha256" "$ARCHIVE_NAME.version.txt"; do
    test -f "$file"
done
sha256sum -c "$ARCHIVE_NAME.sha256"
smb "mkdir \"$remote_temporary\""
remote_created=1
for file in "$ARCHIVE_NAME" "$ARCHIVE_NAME.sha256" "$ARCHIVE_NAME.version.txt"; do
    smb "put \"$file\" \"$remote_temporary/$file\""
    # SMB 不提供通用服务端 SHA256 命令，回读后逐字节校验三个文件。
    smb "get \"$remote_temporary/$file\" \"$private_dir/$file\""
    cmp -- "$file" "$private_dir/$file" || {
        echo "SMB_PUBLISH verification_failed: $file" >&2; exit 1;
    }
    rm -f -- "$private_dir/$file"
done
# 不使用 -f，不覆盖同名版本；校验通过后才让正式版本目录可见。
smb "rename \"$remote_temporary\" \"$RELEASE_VERSION\""
remote_created=0
echo "SMB_PUBLISH success: smb:$SMB_SHARE/$RELEASE_VERSION/$ARCHIVE_NAME"
