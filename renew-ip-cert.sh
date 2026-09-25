#!/bin/sh
# 裸 IP 证书的续期 + 部署。cron 每天跑一次：
#     17 3 * * * sh /app/gateway/renew-ip-cert.sh >> /var/log/renew-ip-cert.log 2>&1
#
# 为什么要自动续：Let's Encrypt 的 IP 证书只有 160 小时有效期，手工签根本来不及，
# 续期必须无人值守。
#
# 为什么用 --deploy-hook 而不是「续完就同步」：lego 只在确实需要续的时候才签发，
# 把「复制证书 + reload nginx」放进 hook，就不会每天空转一次 reload。
#
# 钩子里刻意不读 lego 的环境变量（LEGO_CERT_DOMAIN 之类）：5.5.2 上并不存在，
# 实测会以 "unbound variable" 直接失败 —— 而钩子失败是静默的，等发现时证书早已过期。
# 路径本来就是这个脚本自己传的，直接算出来更稳。
#
# 该不该续由 lego 自己判断：优先用 ARI（RFC 9773 的 renewalInfo 接口）给的时间点，
# 取不到就退回「寿命过半」—— 对 160 小时的证书即还剩约 80 小时时续。

set -eu

IP=47.109.29.134
LEGO_PATH=/root/lego
WEBROOT=/app/gateway/acme
CERTS=/app/gateway/certs
SRC="$LEGO_PATH/certificates/$IP"
NAME="ip-$IP"

# ── 被 --deploy-hook 回调：把新证书落到 certs/，再让 nginx 重新读 ──────
# 也可以手工跑一次来把 /root/lego 里现有的证书同步过去（不消耗签发额度）。
if [ "${1:-}" = "--deploy" ]; then
    if [ ! -f "$SRC.crt" ]; then
        echo "找不到 $SRC.crt，不同步" >&2
        exit 1
    fi

    # 先写 .new 再改名：中途挂了，certs/ 里的旧证书还是完整的，不会两头不沾
    cat "$SRC.crt" "$SRC.issuer.crt" > "$CERTS/$NAME.pem.new"
    cp "$SRC.key" "$CERTS/$NAME.key.new"
    chmod 644 "$CERTS/$NAME.pem.new"
    chmod 600 "$CERTS/$NAME.key.new"
    mv "$CERTS/$NAME.pem.new" "$CERTS/$NAME.pem"
    mv "$CERTS/$NAME.key.new" "$CERTS/$NAME.key"

    # 证书目录是 bind mount，写盘即见；但 nginx 要 reload 才会重新读
    docker compose -f /app/gateway/docker-compose.yml exec -T gateway nginx -s reload
    echo "已换上新证书：$(openssl x509 -in "$CERTS/$NAME.pem" -noout -enddate)"
    exit 0
fi

mkdir -p "$WEBROOT"

exec lego run \
    -d "$IP" \
    --accept-tos \
    --http --http.webroot "$WEBROOT" \
    --profile shortlived \
    --path "$LEGO_PATH" \
    --deploy-hook "sh $0 --deploy"
