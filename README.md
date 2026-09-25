# gateway — 统一入口网关

独占宿主机 **80 / 443 端口**，按**路径前缀**把请求分发给各个项目。所有项目共用这一个入口，
不需要各自占用一个端口，也不需要记不同的端口号。

## 当前架构（运行时）

```
                        Internet
                           │
                  :80/:443 ┌──▼───────────────┐
                           │ gateway          │  只做分发，不存业务数据
                           │ (nginx:alpine)   │  配置 = nginx.conf + routes.inc
                           └──┬───────────────┘
                              │  加入 Docker 外部网络 web
        ┌─────────────────────┼─────────────────────┐
        │                     │                     │
   /guitar/*             /chat/*               /md/*
        │                     │                     │
   ┌────▼─────┐          ┌────▼─────┐         ┌────▼─────┐
   │xianji-web│          │ chat-web │         │  md-web  │  前端：内网 80，expose，不碰宿主机端口
   └────┬─────┘          └────┬─────┘         └────┬─────┘
        │                     │                     │
   ┌────▼─────┐          ┌────▼─────┐         ┌────▼─────┐
   │xianji-api│          │ chat-api │         │  md-api  │  后端：绑 127.0.0.1，只经网关访问
   │  :5000   │          │  :5001   │         │  :5002   │
   └──────────┘          └──────────┘         └──────────┘
```

路径路由写在 `routes.inc` 里，由 80 兜底块和 443 块各 `include` 一次——两个入口共用
同一份路由表，新增项目只改一处。

一句话概括：**只有网关碰宿主机的 80 / 443，各项目全部退到 `web` 网络后面，靠别名互相认识。**

### 服务器实况

| 项 | 值 |
|---|---|
| 服务器 | 任意一台 Linux 主机，四个项目分别落在 `/app/{gateway,chat,xianji,md}` |
| SSH | `ssh <你的用户名>@<你的服务器IP>` |
| 共享网络 | Docker 外部网络 `web`（`docker network create web`，与项目生命周期无关） |
| 网关容器 | `gateway-gateway-1` → 发布 `0.0.0.0:80`、`0.0.0.0:443` |
| 对外域名 | `www.liujn.fun`、`www.jnnnn.top`（都指向本机，各一张证书，路径完全相同） |
| TLS 证书 | `/app/gateway/certs/`（宿主机目录，**不在仓库里**，见下文） |
| chat 容器 | `chat-chat-frontend-1`（内网 80）、`chat-chat-backend-1`（`127.0.0.1:5001`） |
| xianji 容器 | `xianji-frontend-1`（内网 80）、`xianji-backend-1`（`127.0.0.1:5000`）、`xianji-backup-1` |
| md 容器 | `md-md-frontend-1`（内网 80）、`md-md-backend-1`（`127.0.0.1:5002`） |
| 聊天记录落盘 | `/app/chat/server/data/database.sqlite`（宿主机 bind mount，容器重建不丢） |
| 文稿落盘 | `/app/md/server/data/md/*.md`（宿主机 bind mount，**项目里唯一不可从 GitHub 恢复的数据**） |

> 重启网关只是几毫秒的事，也不影响任何项目的数据——它自己不存东西。
> 反过来，停掉任何一个项目，只影响它自己那条路径，其余站点照常。

### 容量与内存（踩过坑，这几条别改回去）

| 项 | 值 |
|---|---|
| CPU / 内存 | **2 核 / 1.7GB** |
| Swap | **2GB**（`/swapfile`，写在 `/etc/fstab`） |
| 磁盘 | 40G（xfs），用了约 16G |
| 空闲余量 | `available` 只有 **600MB 出头**（`dockerd` 自己就占 229MB） |

四个项目全挤在这一台机器上，**余量非常小**。往上加服务、或者跑 `docker build`
（pip / npm install 的峰值轻松几百 MB），都可能把内存打满。

**2026-09-25 出过一次事故**：所有站点「打不开」持续了十几分钟，根因不是网关、也不是证书。

- 系统自带的定时任务 `dnf-makecache.timer`（每隔 1~2 小时跑一次，作用只是给
  「手动装系统包」预热元数据缓存）在 12:33 触发，要从 EPEL 等源下载并建索引，
  峰值要 **682MB**——超过余量。
- 建索引磨了 **16 分钟**、CPU 吃满（2 核跑满 1.6 核 ≈ 监控上看到的 80%），
  12:49 被内核 OOM killer 杀掉。这 16 分钟里整机被拖住，所有请求超时。
- 当时**完全没配 swap**，内核只能杀进程，没有别的选择。

事后做了五处调整，**都可逆**，退回方式在最右列：

| 改动 | 为什么 | 怎么退回 |
|---|---|---|
| 关 `dnf-makecache.timer` | 它只为「手动装包」预热缓存，而这台机器上所有服务都是 Docker 跑的，纯属负担 | `systemctl enable --now dnf-makecache.timer` |
| 加 2GB `/swapfile` | 零 swap 太脆，内存一有尖峰就直接杀进程 | `swapoff /swapfile`，再删掉 `/etc/fstab` 里那行 |
| `vm.swappiness` **0 → 60** | 原来 `/etc/sysctl.conf` 第 1 行写死 0，含义是「**宁可 OOM 也不换页**」——不改这个，新加的 swap 等于白加 | 备份在 `/etc/sysctl.conf.bak-*` |
| 关 `epel` / `epel-cisco-openh264` | 用不上，却每次让 makecache 多下 20MB。EPEL 的 vendor 包里只有 `epel-release` 自己，**没有任何程序依赖它** | `dnf config-manager --set-enabled epel epel-cisco-openh264` |
| 关宿主机 `nginx` | 它是 `enabled`（开机自启）且配置里 `listen 80`，而 80 归 `docker-proxy`——**重启后谁先抢到 80 是掷硬币**：nginx 赢则网关容器绑不上端口，四个站点全挂；ACME 续期也走 80，会连累裸 IP 证书静默续期失败 | `systemctl enable --now nginx` |

> 宿主机**不需要** nginx——网关的 nginx 跑在容器里。宿主机那份是早期没上 Docker 时的遗留
> （配置目录还是 Debian 那套 `sites-available/sites-enabled` 布局，里面留着 `guitar_tabs`、
> `sillytavern.conf.bak`）。

`baseos` / `appstream` / `crb` / `extras` / `docker-ce-stable` / `nginx-stable`
这六个仓库**必须留着**：前四个是 Rocky 基础源（关了系统更新就废了），
后两个对应的 docker 和 nginx 都是 rpm 装的。

再遇到「机器卡住 / 站点打不开」，先跑这个，比查 nginx、DNS、证书快得多：

```bash
ssh root@<服务器IP> 'dmesg -T | grep -iE "out of memory|oom-kill" | tail; cat /proc/loadavg; free -m'
```

**高 loadavg + 实际 CPU 却 90% 以上 idle** = 尖峰刚结束、或被 OOM 打断，就是这条线索。

> 同类定时任务还剩 `mlocate-updatedb.timer`（每天 00:00 全盘扫描），至今没出过事，
> 但性质和 `dnf-makecache` 一样，真被压到可以一并关掉。

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
| `gateway`（本仓库） | `/app/gateway` | 独占 80 / 443，按路径分发 | 全部 |
| `chat` | `/app/chat` | AI 对话 | `/chat/`、`/chat-api/` |
| `xianji` | `/app/xianji` | 弦集吉他谱 | `/guitar/`、`/guitar-api/`、`/guitar-images/` |
| `md` | `/app/md` | 文稿（markdown 阅读 / 编辑） | `/md/`、`/md-api/` |

访问入口：

- `https://www.liujn.fun/` → 302 跳到 `/guitar/`（弦集）
- `https://www.jnnnn.top/` → 同上，两个域名的路径完全一致
- `https://www.liujn.fun/chat/` → 对话
- `https://www.liujn.fun/guitar/` → 弦集
- `https://www.liujn.fun/md/` → 文稿
- `http://<服务器IP>/guitar/` → 同上，走 HTTP（证书签的是域名、没签 IP，所以裸 IP 不做跳转）

## 约定

**每个项目都通过 Docker 外部网络 `web` 互联，并用两个别名暴露自己：**

| 别名 | 指向什么 | 例子 |
|---|---|---|
| `<项目名>-web` | 该项目的前端（内部 nginx，监听 80） | `xianji-web`、`chat-web`、`md-web` |
| `<项目名>-api` | 该项目的后端 API | `xianji-api:5000`、`chat-api:5001`、`md-api:5002` |

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

**只有网关发布 `ports: "80:80"` / `"443:443"`。** 业务项目一律用 `expose`，
绝不碰宿主机的 80 / 443。

**路径路由只写在 `routes.inc` 里，别写进 `nginx.conf`。** `nginx.conf` 里那三个 server 块
（80 域名跳转 / 80 兜底 / 443 主入口）会各 `include` 一次，路由写在 server 块里会导致
「域名能访问、裸 IP 不能」这种只坏一半的现象。

**网关里 `proxy_pass` 必须写成变量形式**（`set $upstream ...; proxy_pass $upstream;`）
并配 `resolver 127.0.0.11`。理由见 `nginx.conf` 顶部注释——简言之：字面主机名是启动时
解析一次，会让各项目互相拖累。

## 新增一个项目

1. **建项目仓库**，`docker-compose.yml` 里：
   - 接入外部网络 `web`，用 `<项目名>-web` / `<项目名>-api` 两个别名；
   - 前端用 `expose: "80"`，后端绑 `127.0.0.1:<端口>`，**都不要碰 80**；
   - 参照 `chat` 或 `xianji` 的 compose 抄即可。
2. **在本仓库 `routes.inc` 里加路由**：照抄文件末尾那段注释掉的模板，把路径指到
   上面两个别名。`location = /<路径> { return 301 ...; }` 那段也一并加上。
3. `git push`。CI 自动同步到 `/app/gateway` 并 `docker compose up -d`。

**只有第 2 步会重启网关（约 1 秒），其它项目完全不受影响。**

最后别忘了在云厂商安全组里，如果新项目不是走 80 而是另开端口，需要单独放行；
走网关的项目则不需要额外放行任何端口。

## HTTPS 证书

两个域名各一张证书，每张都同时覆盖带 `www` 和不带 `www` 的名字：

| 域名 | 文件（都在 `/app/gateway/certs/`） | 有效期 |
|---|---|---|
| `www.liujn.fun`、`liujn.fun` | `www.liujn.fun.pem` / `.key` | 2026-09-19 → **2026-12-17** |
| `www.jnnnn.top`、`jnnnn.top` | `www.jnnnn.top.pem` / `.key` | 2026-09-20 → **2026-12-18** |

- **签发**：DigiCert Encryption Everywhere DV，免费 90 天，**不自动续期**，到期要重新申请
- **容器内路径**：`/etc/nginx/certs/`（只读挂载）
- **引用处**：`nginx.conf` 里对应域名的 443 server 块
- **加第三个域名**：签证书 → scp 进 `certs/` → 加一个 443 块 + 把域名补进那个
  80 跳转块的 `server_name`。`check-certs.sh` 会自动认出新证书，不用改它。

### 为什么不进仓库

`.key` 是私钥，提交上去等于把私钥永久留在 git 历史里。所以：

- `.gitignore` 忽略 `certs/`
- CI 的 rsync 带 `--exclude='certs'`——既不上传，也让 `--delete` 不会把它删掉
  （rsync 默认不删除被 exclude 的文件）

续期用的 `acme/` 也一起 `--exclude` 掉了。它的理由和证书不同（里面没有私钥，
纯粹是运行时目录），但不排除的话每次部署都会清空它，正好把验证中的挑战删掉。

证书只存在于服务器上，**换机器时要手工再传一次**。

### 续期（到期前）

在本地拿到新的 `.pem` / `.key` 后：

```bash
scp www.liujn.fun.pem www.liujn.fun.key root@<服务器IP>:/app/gateway/certs/
ssh root@<服务器IP> 'chmod 600 /app/gateway/certs/www.liujn.fun.key && \
                     cd /app/gateway && docker compose restart gateway'
```

`restart` 会重新加载证书（几毫秒），不需要 `--build`，也不影响任何业务项目。

改完顺手验一下：

```bash
echo | openssl s_client -connect www.liujn.fun:443 -servername www.liujn.fun 2>/dev/null \
  | openssl x509 -noout -dates
```

### 裸 IP 证书（唯一自动续期的一张）

裸 IP `47.109.29.134` 另有一张证书，给「拿不到域名、只能连 IP」的客户端用
（典型场景：域名备案还没下来，微信小程序只能靠裸 IP 做真机调试）。

| | |
|---|---|
| 文件 | `/app/gateway/certs/ip-47.109.29.134.pem` / `.key` |
| 签发 | Let's Encrypt，SAN 里直接放 IP（`IP Address:47.109.29.134`） |
| 有效期 | **只有 160 小时**（约 6.6 天），IP 证书只能签短期的 |
| 引用处 | `nginx.conf` 的 443 默认块（`server_name _`） |
| 续期 | 服务器上 cron 每天跑 `/app/gateway/renew-ip-cert.sh`（随仓库部署） |

160 小时不可能手工签，所以这张**必须**靠脚本续：`lego run`（5.x 里它就是续期命令，
该不该续由它按 ARI / 寿命过半自己判断）走 80 端口的 HTTP-01 挑战（token 写在
`/app/gateway/acme/`，由 compose 挂进容器），签发成功后经 `--deploy-hook` 覆盖
`certs/` 里那两个文件再 reload nginx。日志在 `/var/log/renew-ip-cert.log`。

```bash
ssh root@<服务器IP> 'sh /app/gateway/renew-ip-cert.sh'   # 手动跑一次看结果
```

两个容易踩的点：

- **挑战目录不能放在 `certs/` 里**。那个目录是 700，而 nginx worker 以 `nginx`
  用户跑、进不去 700 的目录，会返回 403 而不是把 token 发出去。所以挑战走独立的
  `acme/`（755）。
- **阿里云的备案拦截只看 Host**：Host 是域名且未备案就重置 / 403，Host 是 IP 则放行。
  这既是裸 IP 能走通的原因，也说明这条路只到备案通过为止。另外微信小程序的
  「服务器域名」**不接受 IP**，所以它只能用于开发调试，不能上线。

### 安全组

网关是唯一对外的服务，云厂商安全组需要放行 **80 与 443**。漏放 443 的症状是
`curl https://...` 一直卡住不返回。

## 单独迭代某个项目

直接 push 那个项目的仓库就行，**不需要碰本仓库**：

- 改 chat → `git push` chat 仓库 → CI 重建 `chat-backend` / `chat-frontend`
- 改 xianji → `git push` xianji 仓库 → CI 重建 xianji 的容器
- 改 md → `git push` md 仓库 → CI 重建 `md-backend` / `md-frontend`

网关和另一个项目完全不知道发生了什么。

**唯一的例外**：如果你改了某个项目的**路径前缀**或**端口**，那必须同步改本仓库的
`routes.inc`，否则网关会把请求发到旧的路径/端口上。

## 部署与排障

四个仓库的 CI 模式一致：push 到 `main` → GitHub Actions 通过 SSH `rsync` 到
`/app/<项目>` → `docker compose up -d`。四个仓库共用同一套 secrets
（`SERVER_SSH_KEY` / `SERVER_HOST` / `SERVER_USER`）。

**本仓库的部署多两道 preflight，且用 `up -d --force-recreate`**（另外三个仓库不需要）：

- `check-certs.sh` 查证书文件是否都在；
- 再用一次性容器跑一遍 `nginx -t` 验配置语法。

两道都在 recreate **之前**跑，任一道失败就中止部署，旧容器照常服务——网关一挂是整站不通，
所以宁可这次部署失败，也不能把坏配置带进线上容器。

之所以必须 `--force-recreate`：`nginx.conf` 是**单文件** bind mount，绑的是 inode，而
rsync 改文件是「写临时文件再改名」，inode 就变了。光 `up -d` 认为「配置没变」而不重建容器，
容器仍读着旧文件——**改了 `nginx.conf` / `routes.inc` 却静默不生效**，且只坏一半（新域名 443
串到旧证书），极难排查。

服务器上常用命令：

```bash
docker ps --format '{{.Names}}\t{{.Ports}}\t{{.Status}}'   # 看容器和端口
docker network inspect web --format '{{range .Containers}}{{.Name}} {{end}}'  # 看谁在 web 网络里
docker compose -f /app/gateway/docker-compose.yml logs -f  # 看网关访问日志
```

改完 `nginx.conf` 或 `routes.inc` 想先验证语法再生效（三个挂载缺一不可，
否则报的错会是「文件找不到」而不是真正的语法问题）。CI 里已经自动跑同一道检查，
下面是手工复现它：

```bash
docker run --rm \
  -v /app/gateway/nginx.conf:/etc/nginx/conf.d/default.conf:ro \
  -v /app/gateway/routes.inc:/etc/nginx/routes.inc:ro \
  -v /app/gateway/certs:/etc/nginx/certs:ro \
  nginx:alpine nginx -t
```

**排障思路**：

- 某条路径 502 → 先看该项目容器是否在跑（`docker ps`），
  再看它有没有正确接入 `web` 网络且别名拼写与 `routes.inc` 一致。
- 网关起不来（`docker compose logs` 里报证书相关）→ `ls /app/gateway/certs/`。
  CI 里有一步专门拦这个：证书不在位时直接中止部署，不会让网关挂掉。
  > 注意网关一挂，**连 HTTP 也一起没了**（80 和 443 是同一个容器），所以宁可部署失败也不要让容器起不来。
- 域名跳 HTTPS 后 `curl` 卡住 → 安全组没放行 443。

## 服务器到期了怎么迁移

四个项目的**代码都在 GitHub**，**业务数据只有宿主机上几个文件**，所以迁移 =
「搬几个文件 + 改 CI secrets + 重跑一次部署」。网关本身无状态，`git clone` 就够，
但它那对 TLS 证书不在仓库里，得单独搬。

### 要备份什么

| 项目 | 宿主机路径 | 内容 | 大小参考 |
|---|---|---|---|
| chat | `/app/chat/.env` | `DEEPSEEK_KEY`、`CHAT_PASSWORD` | 1 KB |
| chat | `/app/chat/server/data/` | SQLite，全部聊天记录 | 几十 KB |
| xianji | `/app/xianji/.env` | `DEEPSEEK_KEY` | 1 KB |
| xianji | `/app/xianji/server/data/` | SQLite，曲谱元数据 | 几百 KB |
| xianji | `/app/xianji/server/images/` | 曲谱图片 | 上百 MB |
| xianji | `/app/xianji/server/backups/` | 自动备份的历史副本 | 可能 1 GB 以上 |
| md | `/app/md/.env` | `MD_PASSWORD` | 1 KB |
| md | `/app/md/server/data/` | 全部 `.md` 文稿 + SQLite（只存登录会话） | 几十 KB |
| gateway | `/app/gateway/certs/` | TLS 证书与私钥 | 几 KB |

三个 `.env` 和网关的 `certs/` **不在任何仓库里**（CI 的 `rsync --delete` 有意排除了它们），
所以**必须单独备份**；`.env` 丢了就得重新去 DeepSeek 申请 Key、重设登录密码，
证书丢了可以去阿里云重新下载（同一张证书可重复下载，不必重新申请）。
`backups/` 只是历史副本，实在搬不动可以放弃。

> **md 的文稿要格外当心**：它和别的项目不一样——chat 的聊天记录、xianji 的曲谱都还能
> 从别处重建，而 `server/data/md/*.md` 是**手写的原始文档，GitHub 上没有任何副本，
> 丢了就是真丢了**。这个目录里的 SQLite 反而无所谓（只有登录会话，重新登录即可）。
> 建议单独、更频繁地把它拉回本地。

### 步骤

**1. 旧机器还登得上时，先打包**

```bash
mkdir -p /root/migrate
tar czf /root/migrate/chat.tgz    -C /app/chat    .env server/data
tar czf /root/migrate/xianji.tgz  -C /app/xianji  .env server/data server/images
tar czf /root/migrate/md.tgz      -C /app/md      .env server/data
tar czf /root/migrate/gateway.tgz -C /app/gateway certs
```

拷回本地（`backups/` 大，按需决定）：

```bash
scp -i <你的私钥> root@<旧IP>:/root/migrate/*.tgz .
```

**2. 新机器准备**

```bash
curl -fsSL https://get.docker.com | sh     # 装 Docker（含 compose 插件）
docker network create web                  # 四个项目共享的外部网络
mkdir -p /app/{gateway,chat,xianji,md}
```

云厂商安全组放行 **80 与 443**——网关是唯一对外的服务，各项目容器都不发布宿主机端口。

**3. 恢复数据**

```bash
tar xzf chat.tgz    -C /app/chat
tar xzf xianji.tgz  -C /app/xianji
tar xzf md.tgz      -C /app/md
tar xzf gateway.tgz -C /app/gateway
chmod 600 /app/gateway/certs/*.key
```

> 证书必须在启动网关**之前**就位：`docker-compose.yml` 把 `./certs` 挂进了容器，
> 读不到证书 nginx 会启动失败，而这个容器同时管着 80 和 443，一挂整站不通。

**4. 部署四个项目**

```bash
cd /app/gateway && git clone https://github.com/<你的用户名>/gateway . && docker compose up -d
cd /app/chat    && git clone https://github.com/<你的用户名>/chat .    && docker compose up -d --build
cd /app/xianji  && git clone https://github.com/<你的用户名>/xianji .  && docker compose up -d --build
cd /app/md      && git clone https://github.com/<你的用户名>/md .      && docker compose up -d --build
```

> 先起网关没关系——它的 `proxy_pass` 是变量形式、按请求解析，项目没起只会让
> 那条路径 502，不会让网关自己起不来。

**5. 改 CI secrets**

四个仓库都要把 `SERVER_HOST` 改成新 IP；如果新机器换了 SSH 密钥，
`SERVER_SSH_KEY` 也要换（`SERVER_USER` 一般不变）。改完随便 push 一次，
看 CI 能否连上新机器，就是最好的验证。

**6. 验证**

```bash
curl -s -o /dev/null -w '%{http_code}\n' http://<新IP>/            # 期望 302 → /guitar/
curl -s -o /dev/null -w '%{http_code}\n' http://<新IP>/chat/       # 期望 200
curl -s -o /dev/null -w '%{http_code}\n' http://<新IP>/md/         # 期望 200
curl -s -o /dev/null -w '%{http_code}\n' https://www.liujn.fun/    # 期望 302 → /guitar/
curl -s -o /dev/null -w '%{http_code}\n' https://www.liujn.fun/chat/  # 期望 200
curl -s -o /dev/null -w '%{http_code}\n' https://www.liujn.fun/md/    # 期望 200
curl -s -o /dev/null -w '%{http_code}\n' https://www.liujn.fun/md-api/auth-check  # 期望 401
curl -s -o /dev/null -w '%{http_code}\n' -L http://www.liujn.fun/  # 期望 200（80 跳 443）
```

**443 一律返回 000 / 卡住**，八成是安全组没放行 443，不是 nginx 的问题。

再手动确认 `/chat/` 能用 `.env` 里的密码登录、`/guitar/` 能正常浏览曲谱、
`/md/` 能用密码进去并看到文稿列表。

**7. 收尾**

域名 A 记录改指向新 IP 即可，nginx 完全不用改（`server_name` 已经是 `liujn.fun` 两个名字）。

> **注意**：如果旧机器已经过期停机、SSH 都进不去，上面的数据就取不出来了。
> 真正保命的是**平时就有备份**，而不是等过期了才想搬。xianji 有 `backup` 容器
> 在定时备份，chat 没有——建议定期手动把上表的文件拉回本地。
