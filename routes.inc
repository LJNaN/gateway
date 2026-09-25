# 路径路由表 —— 被 nginx.conf 里 80 兜底块和每个 443 域名块 include。
#
# 抽成单独文件的原因：这些 server 块要用同一套路由。写两份的话，新增项目时
# 很容易只改一边，导致「有的入口能访问、有的不能」这种怪现象。
#
# 这里只放 location；其余指令（resolver、client_max_body_size 等）都在 nginx.conf 里。

# ── 网关自检端点 ──────────────────────────────────────────────────
# 只证明 nginx 本身在正常应答，不碰任何上游项目。
#
# 为什么放在路由表里：容器 healthcheck 打的就是它，而本文件被 80 兜底块和每个
# 443 块各 include 一次，所以放这儿等于每个入口都自动有，加新域名也不用再补。
#
# 为什么不去探 /guitar/ 之类的上游路径：上游项目挂掉不代表网关有问题，混在
# 一起会让网关替别人背锅、被误判成不健康。
#
# `=` 是精确匹配，优先级高于下面所有前缀 location，不会被任何路由吃掉。
location = /healthz {
    access_log off;
    # stock nginx.conf 把 default_type 设成了 application/octet-stream，
    # 不覆盖的话浏览器打开这个地址会变成「下载文件」而不是显示 ok
    default_type text/plain;
    return 200 "ok\n";
}

# 根路径导向弦集
location = / {
    return 302 /guitar/;
}

# ============ 弦集（xianji） ============

location = /guitar {
    return 301 /guitar/;
}

location /guitar/ {
    set $upstream http://xianji-web:80;
    proxy_pass $upstream;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
}

location /guitar-api/ {
    set $upstream http://xianji-api:5000;
    proxy_pass $upstream;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
}

location /guitar-images/ {
    set $upstream http://xianji-api:5000;
    proxy_pass $upstream;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
}

# ============ 对话（chat） ============

location = /chat {
    return 301 /chat/;
}

location /chat/ {
    set $upstream http://chat-web:80;
    proxy_pass $upstream;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
}

# SSE 流式接口：必须关闭缓冲，否则回复会攒成一坨再返回，看不到打字机效果
location /chat-api/ {
    set $upstream http://chat-api:5001;
    proxy_pass $upstream;
    proxy_http_version 1.1;
    proxy_set_header Connection '';
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;

    proxy_buffering off;
    proxy_cache off;
    chunked_transfer_encoding off;
    proxy_read_timeout 300s;
}

# ============ 文稿（md） ============

location = /md {
    return 301 /md/;
}

location /md/ {
    set $upstream http://md-web:80;
    proxy_pass $upstream;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
}

# 纯 JSON 接口，没有 SSE，不需要 chat 那套关缓冲和长超时
location /md-api/ {
    set $upstream http://md-api:5002;
    proxy_pass $upstream;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
}

# ============ 作品集（xiaoou-portfolio） ============
# 这个项目只有接口和图片，没有前端页面，所以没有「/<路径>/」和 return 301 那两段。
# 图片的长缓存由 FastAPI 自己发（URL 带版本号时 immutable 一年），这里不要覆盖。

location /portfolio-api/ {
    set $upstream http://xiaoou-api:5003;
    proxy_pass $upstream;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
}

location /portfolio-images/ {
    set $upstream http://xiaoou-api:5003;
    proxy_pass $upstream;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
}

# ── 新项目照抄下面这段（记得 return 301 那段也加上）────────────
# location = /<路径> {
#     return 301 /<路径>/;
# }
#
# location /<路径>/ {
#     set $upstream http://<项目名>-web:80;
#     proxy_pass $upstream;
#     proxy_set_header Host $host;
#     proxy_set_header X-Real-IP $remote_addr;
#     proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
# }
#
# location /<路径>-api/ {
#     set $upstream http://<项目名>-api:<后端端口>;
#     proxy_pass $upstream;
#     proxy_set_header Host $host;
#     proxy_set_header X-Real-IP $remote_addr;
#     proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
# }
