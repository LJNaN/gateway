# 路径路由表 —— 被 nginx.conf 里 80 兜底块和 443 主入口 include 两次。
#
# 抽成单独文件的原因：域名走 443、裸 IP 走 80，两个 server 块要用同一套路由。
# 写两份的话，新增项目时很容易只改一边，导致「域名能访问、IP 不能」这种怪现象。
#
# 这里只放 location。server 级指令（client_max_body_size、resolver）在两个
# server 块里各自声明，因为 server_name / listen 不同。

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
