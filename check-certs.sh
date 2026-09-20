#!/bin/sh
# 部署前检查：nginx.conf 里 ssl_certificate / ssl_certificate_key 指到的文件，
# 在宿主机上是不是真的存在。
#
# 为什么要单独查一道：nginx 读不到证书会直接启动失败，而这个容器同时管着
# 80 和 443，一挂整站不通——比「这次部署失败」严重得多。所以宁可中止部署，
# 也不要把容器弄得起不来。
#
# 要查哪些文件是从 nginx.conf 里现取的，所以以后加域名、改证书文件名，
# 都不用动这个脚本。
#
# 用法：sh check-certs.sh [nginx.conf 路径] [宿主机 certs 目录]

set -e

CONF=${1:-/app/gateway/nginx.conf}
CERTS_DIR=${2:-/app/gateway/certs}

if [ ! -r "$CONF" ]; then
    echo "读不到 $CONF，无法校验证书，已中止部署。"
    exit 1
fi

# 证书文件不在仓库里（私钥不能提交），只存在于服务器上，所以只能在这里查。
#
# 先去掉注释行：nginx.conf 里解释证书放哪的注释也含 "/etc/nginx/certs/"，
# 不去掉会把它当引用去查，误报缺文件、白白拦下部署。
# 文件名限定为 [A-Za-z0-9._-]，这样注释里的 /etc/nginx/certs/（只读）之类
# 不会匹配上（后面跟的是中文括号，不在这个字符集里）。
refs=$(sed 's/#.*//' "$CONF" \
    | grep -oE '/etc/nginx/certs/[A-Za-z0-9._-]+' \
    | sort -u || true)

if [ -z "$refs" ]; then
    echo "$CONF 里没有任何 ssl_certificate 引用。"
    echo "本脚本是给「配了 HTTPS 的网关」兜底的，配置里查不到证书说明预期不符，已中止部署。"
    echo "如果确实是刻意去掉了 HTTPS，那也应该顺手删掉这个检查步骤。"
    exit 1
fi

missing=0
for ref in $refs; do
    host_path="$CERTS_DIR/${ref#/etc/nginx/certs/}"
    if [ -f "$host_path" ]; then
        echo "  OK    $host_path"
    else
        echo "  缺失  $host_path"
        missing=1
    fi
done

if [ "$missing" -ne 0 ]; then
    echo ""
    echo "证书不全，nginx 会起不来，已中止部署。"
    echo "手工 scp 证书到 $CERTS_DIR/ 后重试（见 README「HTTPS 证书」）。"
    exit 1
fi

echo "证书检查通过"
