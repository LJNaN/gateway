# gateway — 统一入口网关

独占宿主机 **80 端口**，按**路径前缀**把请求分发给各个项目。所有项目共用这一个入口，
不需要各自占用一个端口，也不需要记不同的端口号。

## 当前架构（运行时）

```
                        Internet
                           │
                    :80 ┌──▼───────────────┐
                        │ gateway          │  只做分发，不存业务数据
                        │ (nginx:alpine)   │  配置 = gateway/nginx.conf
                        └──┬───────────────┘
                           │  加入 Docker 外部网络 web
        ┌──────────────────┼──────────────────┐
        │                  │                  │
   /guitar/*          /chat/*            （以后新增的项目）
        │                  │
   ┌────▼─────┐       ┌────▼─────┐
   │xianji-web│       │ chat-web │   前端：内网 80，用 expose，不碰宿主机端口
   └────┬─────┘       └────┬─────┘
        │                  │
   ┌────▼─────┐       ┌────▼─────┐
   │xianji-api│       │ chat-api │   后端：绑 127.0.0.1，只经网关访问
   │  :5000   │       │  :5001   │
   └──────────┘       └──────────┘
```

一句话概括：**只有网关碰宿主机的 80，各项目全部退到 `web` 网络后面，靠别名互相认识。**

### 服务器实况

| 项 | 值 |
|---|---|
| 服务器 | 任意一台 Linux 主机，三个项目分别落在 `/app/{gateway,chat,xianji}` |
| SSH | `ssh <你的用户名>@<你的服务器IP>` |
| 共享网络 | Docker 外部网络 `web`（`docker network create web`，与项目生命周期无关） |
| 网关容器 | `gateway-gateway-1` → 发布 `0.0.0.0:80` |
| chat 容器 | `chat-chat-frontend-1`（内网 80）、`chat-chat-backend-1`（`127.0.0.1:5001`） |
| xianji 容器 | `xianji-frontend-1`（内网 80）、`xianji-backend-1`（`127.0.0.1:5000`）、`xianji-backup-1` |
| 聊天记录落盘 | `/app/chat/server/data/database.sqlite`（宿主机 bind mount，容器重建不丢） |

> 重启网关只是几毫秒的事，也不影响任何项目的数据——它自己不存东西。
> 反过来，停掉任何一个项目，只影响它自己那条路径，其余站点照常。

## 为什么网关要独立成一个仓库

网关是**共享基础设施**，不属于任何一个业务项目。早期它挂在 chat 项目的 compose 里，
带来两个问题：

- **改 chat 会波及 xianji**。chat 的部署命令是 `docker compose up -d`，会把网关容器一起
  重建，80 端口抖一下，xianji 跟着不可用。
- **停 chat 会停掉所有人**。网关是 chat compose 里的一个 service，`docker compose down`
  一执行，网关没了，xianji 也进不去了。

拆出来之后，网关只在自己的仓库里迭代；各项目 push 只重建自己，互不影响。

## 目录与仓库对应关系

| 仓库 | 服务器目录 | 职责 | 占用的路径 |
|---|---|---|---|
| `gateway`（本仓库） | `/app/gateway` | 独占 80，按路径分发 | 全部 |
| `chat` | `/app/chat` | AI 对话 | `/chat/`、`/chat-api/` |
| `xianji` | `/app/xianji` | 弦集吉他谱 | `/guitar/`、`/guitar-api/`、`/guitar-images/` |

访问入口：

- `http://<服务器IP>/` → 302 跳到 `/guitar/`（弦集）
- `http://<服务器IP>/chat/` → 对话
- `http://<服务器IP>/guitar/` → 弦集

## 约定

**每个项目都通过 Docker 外部网络 `web` 互联，并用两个别名暴露自己：**

| 别名 | 指向什么 | 例子 |
|---|---|---|
| `<项目名>-web` | 该项目的前端（内部 nginx，监听 80） | `xianji-web`、`chat-web` |
| `<项目名>-api` | 该项目的后端 API | `xianji-api:5000`、`chat-api:5001` |

别名在**项目自己的** `docker-compose.yml` 里声明，例如：

```yaml
services:
  frontend:
    networks:
      default:
      web:
        aliases:
          - <项目名>-web

networks:
  web:
    external: true
```

**只有网关发布 `ports: "80:80"`。** 业务项目一律用 `expose`，绝不碰宿主机的 80。

**网关里 `proxy_pass` 必须写成变量形式**（`set $upstream ...; proxy_pass $upstream;`）
并配 `resolver 127.0.0.11`。理由见 `nginx.conf` 顶部注释——简言之：字面主机名是启动时
解析一次，会让各项目互相拖累。

## 新增一个项目

1. **建项目仓库**，`docker-compose.yml` 里：
   - 接入外部网络 `web`，用 `<项目名>-web` / `<项目名>-api` 两个别名；
   - 前端用 `expose: "80"`，后端绑 `127.0.0.1:<端口>`，**都不要碰 80**；
   - 参照 `chat` 或 `xianji` 的 compose 抄即可。
2. **在本仓库 `nginx.conf` 里加路由**：照抄文件末尾那段注释掉的模板，把路径指到
   上面两个别名。文件顶部 `location = /<路径> { return 301 ...; }` 那段也一并加上。
3. `git push`。CI 自动同步到 `/app/gateway` 并 `docker compose up -d`。

**只有第 2 步会重启网关（约 1 秒），其它项目完全不受影响。**

最后别忘了在云厂商安全组里，如果新项目不是走 80 而是另开端口，需要单独放行；
走网关的项目则不需要额外放行任何端口。

## 单独迭代某个项目

直接 push 那个项目的仓库就行，**不需要碰本仓库**：

- 改 chat → `git push` chat 仓库 → CI 重建 `chat-backend` / `chat-frontend`
- 改 xianji → `git push` xianji 仓库 → CI 重建 xianji 的容器

网关和另一个项目完全不知道发生了什么。

**唯一的例外**：如果你改了某个项目的**路径前缀**或**端口**，那必须同步改本仓库的
`nginx.conf`，否则网关会把请求发到旧的路径/端口上。

## 部署与排障

三个仓库的 CI 模式一致：push 到 `main` → GitHub Actions 通过 SSH `rsync` 到
`/app/<项目>` → `docker compose up -d`。三个仓库共用同一套 secrets
（`SERVER_SSH_KEY` / `SERVER_HOST` / `SERVER_USER`）。

服务器上常用命令：

```bash
docker ps --format '{{.Names}}\t{{.Ports}}\t{{.Status}}'   # 看容器和端口
docker network inspect web --format '{{range .Containers}}{{.Name}} {{end}}'  # 看谁在 web 网络里
docker compose -f /app/gateway/docker-compose.yml logs -f  # 看网关访问日志
```

改完 `nginx.conf` 想先验证语法再生效：

```bash
docker run --rm -v /app/gateway/nginx.conf:/etc/nginx/conf.d/default.conf:ro nginx:alpine nginx -t
```

**排障思路**：某条路径 502 → 先看该项目容器是否在跑（`docker ps`），
再看它有没有正确接入 `web` 网络且别名拼写与 `nginx.conf` 一致。

## 服务器到期了怎么迁移

三个项目的**代码都在 GitHub**，**业务数据只有宿主机上几个文件**，所以迁移 =
「搬几个文件 + 改 CI secrets + 重跑一次部署」。网关自己无状态，不用备份，`git clone` 就够。

### 要备份什么

| 项目 | 宿主机路径 | 内容 | 大小参考 |
|---|---|---|---|
| chat | `/app/chat/.env` | `DEEPSEEK_KEY`、`CHAT_PASSWORD` | 1 KB |
| chat | `/app/chat/server/data/` | SQLite，全部聊天记录 | 几十 KB |
| xianji | `/app/xianji/.env` | `DEEPSEEK_KEY` | 1 KB |
| xianji | `/app/xianji/server/data/` | SQLite，曲谱元数据 | 几百 KB |
| xianji | `/app/xianji/server/images/` | 曲谱图片 | 上百 MB |
| xianji | `/app/xianji/server/backups/` | 自动备份的历史副本 | 可能 1 GB 以上 |
| gateway | — | 无状态 | — |

两个 `.env` **不在任何仓库里**（CI 的 `rsync --delete` 有意排除了它们），
所以**必须单独备份**；丢了就得重新去 DeepSeek 申请 Key、重设登录密码。
`backups/` 只是历史副本，实在搬不动可以放弃。

### 步骤

**1. 旧机器还登得上时，先打包**

```bash
mkdir -p /root/migrate
tar czf /root/migrate/chat.tgz   -C /app/chat   .env server/data
tar czf /root/migrate/xianji.tgz -C /app/xianji .env server/data server/images
```

拷回本地（`backups/` 大，按需决定）：

```bash
scp -i <你的私钥> root@<旧IP>:/root/migrate/*.tgz .
```

**2. 新机器准备**

```bash
curl -fsSL https://get.docker.com | sh     # 装 Docker（含 compose 插件）
docker network create web                  # 三个项目共享的外部网络
mkdir -p /app/{gateway,chat,xianji}
```

云厂商安全组放行 **80**——网关是唯一对外的端口，各项目容器都不发布宿主机端口。

**3. 恢复数据**

```bash
tar xzf chat.tgz   -C /app/chat
tar xzf xianji.tgz -C /app/xianji
```

**4. 部署三个项目**

```bash
cd /app/gateway && git clone https://github.com/<你的用户名>/gateway . && docker compose up -d
cd /app/chat    && git clone https://github.com/<你的用户名>/chat .    && docker compose up -d --build
cd /app/xianji  && git clone https://github.com/<你的用户名>/xianji .  && docker compose up -d --build
```

> 先起网关没关系——它的 `proxy_pass` 是变量形式、按请求解析，项目没起只会让
> 那条路径 502，不会让网关自己起不来。

**5. 改 CI secrets**

三个仓库都要把 `SERVER_HOST` 改成新 IP；如果新机器换了 SSH 密钥，
`SERVER_SSH_KEY` 也要换（`SERVER_USER` 一般不变）。改完随便 push 一次，
看 CI 能否连上新机器，就是最好的验证。

**6. 验证**

```bash
curl -s -o /dev/null -w '%{http_code}\n' http://<新IP>/         # 期望 302 → /guitar/
curl -s -o /dev/null -w '%{http_code}\n' http://<新IP>/chat/    # 期望 200
curl -s -o /dev/null -w '%{http_code}\n' http://<新IP>/guitar/  # 期望 200
```

再手动确认 `/chat/` 能用 `.env` 里的密码登录、`/guitar/` 能正常浏览曲谱。

**7. 收尾**

以后要是买了域名，把 A 记录指向新 IP 即可，nginx 完全不用改。

> **注意**：如果旧机器已经过期停机、SSH 都进不去，上面的数据就取不出来了。
> 真正保命的是**平时就有备份**，而不是等过期了才想搬。xianji 有 `backup` 容器
> 在定时备份，chat 没有——建议定期手动把上表的文件拉回本地。
