# ChatServer

在 Linux 环境下基于 muduo 网络库开发的集群聊天服务器。实现功能如下：
- 用户注册Register
- 用户登录Login
- 添加好友AddFriend
- 添加群组AddGroup
- 好友一对一聊天 OneChat
- 群组聊天（群发消息）GroupChat
- 离线消息记录等功能。

## 项目特点

- 网络层，业务层，数据层，客户端页面分层设计
- 基于 muduo 网络库开发网络模块ChatServer，实现客户端和服务器之间连接和发送消息
- 使用Mysql，封装接口实现数据层和业务层的连接，将用户数据储存到磁盘中，实现数据持久化。
- 使用第三方 JSON 库实现通信数据的序列化和反序列化。
- 使用 Nginx 的 TCP 负载均衡功能，将客户端请求分派到多个服务器上，以提高并发处理能力。
- 基于发布-订阅的服务器中间件redis消息队列，解决用户跨服务器无法通信问题。
- 基于 CMake 构建自动化编译环境，使用git对项目进行版本管理。

## 必要环境

- 安装`boost`库
- 安装`muduo`库
- 安装`Nginx`
- 安装`redis`

## 构建项目

创建数据库

```shell
# 连接MySQL
mysql -u root -p your passward
# 创建数据库
create database chat;
# 执行数据库脚本创建表
source chat.sql
```

执行脚本构建项目

```shell
bash build.sh
```

## 执行生成文件

```shell
# 启动服务端
cd ./bin
./ChatServer 6000 
```

```shell
# 启动客户端
./ChatClient 127.0.0.1 8000
```

![1663830571(1).png](https://syz-picture.oss-cn-shenzhen.aliyuncs.com/D:%5CPrograme%20Files(x86)%5CPicGo1663830578650-52d58f18-370f-426a-b8fe-f07dfd06b116.png)

# 项目讲解

## 数据库表设计

**User表** 

| 字段名称 | 字段类型                  | 字段说明     | 约束                        |
| -------- | ------------------------- | ------------ | --------------------------- |
| id       | INT                       | 用户id       | PRIMARY KEY、AUTO_INCREMENT |
| name     | VARCHAR(50)               | 用户名       | NOT NULL, UNIQUE            |
| password | VARCHAR(50)               | 用户密码     | NOT NULL                    |
| state    | ENUM('online', 'offline') | 当前登录状态 | DEFAULT 'offline'           |

**Friend表**

| 字段名称 | 字段类型 | 字段说明 | 约束               |
| -------- | -------- | -------- | ------------------ |
| userid   | INT      | 用户id   | NOT NULL、联合主键 |
| friendid | INT      | 好友id   | NOT NULL、联合主键 |

**AllGroup表**

| 字段名称  | 字段类型     | 字段说明   | 约束                        |
| --------- | ------------ | ---------- | --------------------------- |
| id        | INT          | 组id       | PRIMARY KEY、AUTO_INCREMENT |
| groupname | VARCHAR(50)  | 组名称     | NOT NULL, UNIQUE            |
| groupdesc | VARCHAR(200) | 组功能描述 | DEFAULT ''                  |

**GroupUser表**

| 字段名称  | 字段类型                  | 字段说明 | 约束               |
| --------- | ------------------------- | -------- | ------------------ |
| groupid   | INT                       | 组id     | NOT NULL、联合主键 |
| userid    | INT                       | 组员id   | NOT NULL、联合主键 |
| grouprole | ENUM('creator', 'normal') | 组内角色 | DEFAULT 'normal'   |

**OfflineMessage表**

| 字段名称 | 字段类型    | 字段说明                   | 约束     |
| -------- | ----------- | -------------------------- | -------- |
| userid   | INT         | 用户id                     | NOT NULL |
| message  | VARCHAR(50) | 离线消息（存储Json字符串） | NOT NULL |

## 网络模块设计

muduo 的线程模型为「one loop per thread + threadPool」模型。一个线程对应一个事件循环（EventLoop），也对应着一个 Reactor 模型。EventLoop 负责 IO 和定时器事件的分派。

muduo 是主从 Reactor 模型，有 `mainReactor` 和 `subReactor`。`mainReactor`通过 `Acceptor` 接收新连接，然后将新连接派发到 `subReactor` 上进行连接的维护。这样 `mainReactor` 可以只专注于监听新连接的到来，而从维护旧连接的业务中得到解放。同时多个 `Reactor` 可以并行运行在多核 CPU 中，增加服务效率。因此我们可以通过 muduo 快速完成网络模块。

![](https://cdn.nlark.com/yuque/0/2022/png/26752078/1663324955126-3a8078fe-f271-4a1b-82c7-b75edff3cda8.png?x-oss-process=image%2Fresize%2Cw_720%2Climit_0#crop=0&crop=0&crop=1&crop=1&from=url&height=345&id=Jzfh0&margin=%5Bobject%20Object%5D&originHeight=435&originWidth=720&originalType=binary&ratio=1&rotation=0&showTitle=false&status=done&style=none&title=&width=571)

使用 muduo 注册消息事件到来的回调函数，并根据得到的 `MSGID` 定位到不同的处理函数中。以此实现业务模块和网络模块的解耦。

```cpp
// 上报读写事件相关信息的回调函数
void ChatServer::onMessage(const TcpConnectionPtr &conn,
                           Buffer *buffer,
                           Timestamp time)
{
    string buf = buffer->retrieveAllAsString();
    json js = json::parse(buf);
    
	// 业务模块和网络模块解耦
    auto msgHandler = ChatService::instance()->getHandler(js["msgid"].get<int>());
    // 回调消息绑定好的事件处理器，来执行相应的业务处理
    msgHandler(conn, js, time);
}
```

## 业务模块设计

### 注册模块

我们从网络模块接收数据，根据 `MSGID` 定位到注册模块。从传递过来的 `json` 对象中获取用户 ID 和用户密码。并以此生成 `User` 对象，调用 model 层方法将新生成的 `User` 插入到数据库中。

### 登录模块

从 `json` 对象中获取用户ID和密码，并在数据库中查询获取用户信息是否匹配。如果用户已经登录过，即 `state == "online"`，则返回错误信息。登录成功后需要在改服务端的用户表中记录登录用户，并显示该用户的好友列表和收到的离线消息。

### 客户端异常退出模块

如果客户端异常退出了，我们会从服务端记录用户连接的表中找到该用户，如果它断连了就从此表中删除，并设置其状态为 `offline`。

### 服务端异常退出模块

如果服务端异常退出，它会将所有在线的客户的状态都设置为 `offline`。即，让所有用户都下线。异常退出一般是 `CTRL + C` 时，我们需要捕捉信号。这里使用了 Linux 的信号处理函数，我们向信号注册回调函数，然后在函数内将所有用户置为下线状态。

### 点对点聊天模块

通过传递的 `json` 查找对话用户 ID：

- 用户处于登录状态：直接向该用户发送信息
- 用户处于离线状态：需存储离线消息

### 添加好友模块

从 `json` 对象中获取添加登录用户 ID 和其想添加的好友的 ID，调用 model 层代码在 friend 表中插入好友信息。

### 群组模块

创建群组需要描述群组名称，群组的描述，然后调用 model 层方法在数据库中记录新群组信息。
加入群组需要给出用户 ID 和想要加入群组的 ID，其中会显示该用户是群组的普通成员还是创建者。
群组聊天给出群组 ID 和聊天信息，群内成员在线会直接接收到。

## 使用Nginx负载均衡模块

### 负载均衡是什么

假设一台机器支持两万的并发量，现在我们需要保证八万的并发量。首先想到的是升级服务器的配置，比如提高 CPU 执行频率，加大内存等提高机器的物理性能来解决此问题。但是单台机器的性能毕竟是有限的，而且也有着摩尔定律也日已失效。

这个时候我们就可以增加服务器的数量，将用户请求分发到不同的服务器上分担压力，这就是负载均衡。那我们就需要有一个第三方组件充当负载均衡器，由它负责将不同的请求分发到不同的服务器上。而本项目，我们选择 `Nginx` 的负载均衡功能。

![](https://cdn.nlark.com/yuque/0/2022/png/26752078/1663746624651-351f9bcb-4ed5-40cd-9f2f-1b72c9964316.png#crop=0&crop=0&crop=1&crop=1&from=url&id=i9BRn&margin=%5Bobject%20Object%5D&originHeight=494&originWidth=967&originalType=binary&ratio=1&rotation=0&showTitle=false&status=done&style=none&title=)

选择 `Nginx` 的 `tcp` 负载均衡模块的原因：

1. 把`client`的请求按照负载算法分发到具体的业务服务器`ChatServer`上
2. 能够`ChantServer`保持心跳机制，检测`ChatServer`故障
3. 能够发现新添加的`ChatServer`设备，方便扩展服务器数量

### 有哪些负载均衡策略：
1、轮询（默认）
每个请求按时间顺序逐一分配到不同的后端服务器，如果后端服务器down掉，能自动剔除。
upstream backserver { 
    server 192.168.0.14; 
    server 192.168.0.15; 
} 
2、指定权重
指定轮询几率，weight和访问比率成正比，用于后端服务器性能不均的情况。
    upstream backserver { 
        server 192.168.0.14 weight=8; 
        server 192.168.0.15 weight=10; 
    } 
3、IP绑定 ip_hash
每个请求按访问ip的hash结果分配，这样每个访客固定访问一个后端服务器，可以解决session的问题。
    upstream backserver { 
        ip_hash; 
        server 192.168.0.14:88; 
        server 192.168.0.15:80; 
    } 
4、fair（第三方）
按 后端服务器的响应时间 来分配请求，响应时间短的优先分配。
    upstream backserver { 
        server server1; 
        server server2; 
        fair; 
    } 
### 配置负载均衡

![](https://cdn.nlark.com/yuque/0/2022/png/26752078/1663732379258-4c925576-3374-4f0d-8274-6031a8366536.png#crop=0&crop=0&crop=1&crop=1&from=url&id=ARrJw&margin=%5Bobject%20Object%5D&originHeight=402&originWidth=810&originalType=binary&ratio=1&rotation=0&showTitle=false&status=done&style=none&title=)

配置好后，重新加载配置文件启动。

```shell
/usr/local/nginx/sbin/nginx -s reload
```



## redis发布-订阅功能解决跨服务器通信问题

### 如何保证支持跨服务器通信

我们之前的`ChatServer`是维护了一个连接的用户表，每次向别的用户发消息都会从用户表中查看对端用户是否在线。然后再判断是直接发送，还是转为离线消息。

但是现在我们是集群服务器，有多个服务器维护用户。我们的`ChatServerA`要聊天的对象在`ChatServerB`，`ChatServerA`在自己服务器的用户表中找不到。那么可能对端用户在线，它却给对端用户发送了离线消息。因此，我们需要保证跨服务器间的通信！那我们如何实现，非常直观的想法，我们可以让后端的服务器之间互相连接。

![](https://cdn.nlark.com/yuque/0/2022/png/26752078/1663747492823-3d6b305d-0008-4fce-a1fa-4683f1800adb.png#crop=0&crop=0&crop=1&crop=1&from=url&id=RU1j6&margin=%5Bobject%20Object%5D&originHeight=554&originWidth=698&originalType=binary&ratio=1&rotation=0&showTitle=false&status=done&style=none&title=)

上面的设计，让各个ChatServer服务器互相之间直接建立TCP连接进行通信，相当于在服务器网络之间进行广播。这样的设计使得各个服务器之间耦合度太高，不利于系统扩展，并且会占用系统大量的socket资源，各服务器之间的带宽压力很大，不能够节省资源给更多的客户端提供服务，因此绝对不是一个好的设计。

集群部署的服务器之间进行通信，最好的方式就是引入中间件消息队列，解耦各个服务器，使整个系统松耦合，提高服务器的响应能力，节省服务器的带宽资源，如下图所示：

![](https://cdn.nlark.com/yuque/0/2022/png/26752078/1663747534358-10e307b4-95c8-43f3-8dc2-5deed9893f1c.png#crop=0&crop=0&crop=1&crop=1&from=url&id=QproC&margin=%5Bobject%20Object%5D&originHeight=505&originWidth=619&originalType=binary&ratio=1&rotation=0&showTitle=false&status=done&style=none&title=)

## 开发问题记录
问题：同一个Client用户登录注销之后，再次登录出现阻塞情况
解决：
    1.使用ps -u 查看当前系统中正在运行的进程的命令
    2.使用gdp attach PID 调试
    3.info threads 查看线程数量，发现两个接收输入的进程
    4.用bt打印堆栈，找到对应代码
    主线程中的选择登录之后的发送消息：int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
    主线程中的recv：int len = recv(clientfd, buffer, 1024, 0);
    子线程中的recv：int len = recv(clientfd, buffer, 1024, 0);  
    5.两个的clientfd是一样的，说明我们登录进来之后的发送的消息是被子线程接收了，但是我们子线程没有写对应的处理逻辑
    6.修改代码：原本的逻辑是用户登录之后启动子线程，所以会有两个recv现在改成
    主线程只发送（把他的recv去掉），子线程专门接收
    启动主线程的时候就启动子线程（读线程）


### 历史消息存储
1. 每个用户一个文件夹，存历史消息（按天存储），登陆的时候读取文件夹显示历史消息
### 网络拥堵的时候，如何按序显示消息
1. 给消息加上时间戳（因为服务器和客户端在网络环境不同的情况下有延时，所以不行）
2. 给消息加上序号，服务器维护每个用户发送的消息的seq
   加上了seq，也方便做撤回操作

### 怎么保证消息的可靠传输
1. 使用TCP连接
int ret = send(fd,buf,buf_size,0)  将消息发送到内核空间的TCP的发送缓冲区里
然后内核TCP协议栈将缓冲区消息发送出去

### 除了redis还有什么其他组件
一.我们用的不是redis主业，而是用的它的中间件副业：当成一个中间件做发布-订阅功能
    服务器中间件： 消息队列
    kafka：分布式的消息队列中间件 高并发，高可用，主要做一个日志搜集
    zeromq，rabbitmq，rocketmq：个高性能、异步消息传递库，用于构建分布式和并发应用程序。
二. redis挂了怎么办：
    1.为什么会挂：redis消息积累的过快，消费消息太慢，或者是服务器种redis占用进程过大，直接被linux系统杀掉了
    2.非主业务/流量不是非常大用redis的发布订阅
    不然用专门的mq来做消息队列
    1.消息的发布更可靠，比如当消息在一个channel发布的时候，发现没有人订阅这个channel，redis就会直接丢失，而mq会放到缓存
    2.消息的消费，有一个应答过程，如果没有回答 就会重新推送这个消息
三. 如果这个项目要用redis的主业
    1.将用户的状态存到redis缓存里
    2.用户的状态先到redis缓存查，查不到再到数据库找
四. 为什么要用redis做为跨服务器通信的组件，为什么各个server不能直接进行通信
    所有服务器不用感知其他服务器的存在，只需要做好本职工作就好了
    每台服务器之间要维持连接，如果新加了一台服务器，那么要让这个服务器和所有现有的服务器增加连接，非常麻烦

### 如果网络拥塞严重，Server如何感知Client在线还是掉线了
1.正常情况下如果recv的长度为0，那就判断client下线了
2.connect成功的client分配一个心跳计数
3.Server启动一个心跳计时器：超时1s，那所有Client的心跳计数+1，
4.如果client的心跳计数超过5，就判断client已经掉线了，拆除这个client所有的连接以及其他资源
5.如果client发来了MSG_TYPE:heartbeat，那么Server中对应的心跳计数-1
总结：基于长连接的业务 使用 心跳保持机制

上面的操作是应用层的，当引用层死锁了，那么Server如何判断client是不发消息还是网络卡了还是掉线了
TCP协议中有一个keepalive功能，保活功能（默认是关闭的）
如果开启了，每隔xx时间，会发送一个空的报文段，探测对方是否在线
如果探测没有响应，延迟xx时间，继续发送探测包，如果xx次都没有响应，拆除连接

## proactor模型是什么
1. 在 Reactor 模型中，事件驱动由应用程序负责；在 Proactor 模型中，事件驱动由操作系统或底层的 I/O 服务提供者负责。
2. 在 Reactor 模型中，Reactor 负责监听事件、分发事件和调用相应的处理程序；在 Proactor 中，Proactor 负责管理和调度异步操作，应用程序负责提供完成事件的处理程序。
3. Reactor 模型适用于需要细粒度控制事件处理和资源管理的场景，Proactor 模型则更加简化了异步 I/O 编程，适用于需要处理大量并发连接的高性能应用。

## 异步IO是什么
1. 进行 I/O 操作时，程序不会被阻塞，而是能够在等待数据返回的同时继续执行其他任务。异步 I/O 允许程序在发起 I/O 请求后立即返回，而不必等待操作完成。
2. 异步 I/O 的实现方式通常有两种：基于事件驱动的 Reactor 模型和基于完成事件的 Proactor 模型。

## 对于一个read()函数，从异步IO的角度，会发生哪些系统调用
1. 非阻塞 I/O：在非阻塞 I/O 模式下，read() 函数会立即返回，而不会等待数据的到达。如果没有数据可用，read() 函数会返回一个错误码（比如 EAGAIN 或 EWOULDBLOCK），表明当前没有数据可读。这样，应用程序可以继续执行其他任务，然后稍后再次尝试读取数据。
2. 应用程序会将文件描述符（或套接字）注册到事件监听器上，并在数据准备就绪时得到通知。这样，应用程序就可以在等待数据时继续执行其他任务，而不必轮询或阻塞在 read() 调用上。
3. 异步 I/O（Asynchronous I/O）：某些操作系统提供了原生的异步 I/O 支持，允许应用程序发起读取操作，然后在数据就绪时得到通知。这种方式下，应用程序不会被阻塞，而是可以继续执行其他任务。在类 Unix 系统中，aio_read() 和 aio_suspend() 等函数用于实现异步 I/O。


## 事件驱动模型中，select，poll和epoll函数应该如何选择
1. select、poll和epoll都是用于实现 I/O 多路复用的方式
2. select 是一种比较老的方式，它使用 位图来表示文件描述符(socket)的状态。调用 select 时，内核需要遍历整个位图，检查每个文件描述符是否就绪。这种轮询的方式在连接数量很少时还是很有效的，但当连接数量增多时，性能会下降。
3. poll 使用 链表结构来表示文件描述符的状态，没有最大连接数的限制。和 select 函数一样，poll 返回后，需要轮询来获取就绪的描述符，因此随着监视的描述符数量的增长，其效率也会线性下降。
4. epoll 是 Linux 特有的一种方式，它使用了事件驱动的模型，没有最大连接数的限制。它将文件描述符添加到 epoll 的事件集合中，等待事件的发生。与 select 和 poll 不同的是，epoll 不需要轮询，它使用回调的方式，只关注真正发生事件的文件描述符。这使得epoll在大规模高并发连接下具有卓越的性能。

## 事件驱动模型中，epoll支持哪些触发模式
1. 水平触发（Level-Triggered）：在水平触发模式下，只要文件描述符的状态处于可读或可写状态，epoll 就会持续通知应用程序，直到应用程序对该文件描述符进行了处理。可能会导致事件通知频繁，需要应用程序进行适当的处理以避免性能问题。
2. 边缘触发（Edge-Triggered）：只有当文件描述符上的状态变化时，epoll 才会通知应用程序。状态变化包括数据可读、数据可写等。一旦 epoll 通知应用程序某个文件描述符处于可读或可写状态，应用程序必须立即处理该事件，否则将错过通知。能够最大程度地减少事件通知的次数。

## 如果我有一百万个用户，如何高效查询一堆用户是不是当前登录用户的好友
缓存好友列表：如果好友关系的变动不是非常频繁，可以考虑将用户的好友列表缓存在内存中，或者使用分布式缓存（如 Redis）来存储好友列表。这样可以避免频繁地从数据库中查询好友列表，提升查询效率。

## buffer pool是什么
1. 缓冲池，数据库系统可以将经常访问的数据页缓存到内存中，用来缓存数据和索引数据的，减少磁盘IO
2. 缓存数据页（Page） 和对缓存数据页进行描述的 控制块组成。

## mmap是什么
1. 是一种 Unix/Linux 系统中的系统调用，用于在用户空间和内核空间之间建立内存映射关系。mmap 的全称是 "memory map"，即内存映射，它允许程序将一个文件或者其他对象映射到其虚拟地址空间中，从而实现了文件和内存之间的直接映射关系。

## 引用折叠和完美转发
1. 引用折叠是为了实现完美转发的方法，例如：T& & 折叠成 T&。
2. 完美转发：完美转发的目标就是在函数模板中正确地保持传递参数的引用性质，包括左值引用和右值引用，从而避免不必要的拷贝或移动操作

## shared_ptr在多线程的情况下有什么安全隐患
1. 循环引用：在多线程环境下，循环引用可能会导致资源无法释放，从而导致内存泄漏。比如两个或多个 std::shared_ptr 相互引用，形成了循环引用，即使外部没有引用它们，它们也无法被正确释放。
2. 由于 shared_ptr 是通过引用计数来管理资源的释放的，当最后一个 shared_ptr 指向的资源被释放时，就会释放该资源。但在多线程环境下，当一个线程正在使用资源时，其他线程可能会不小心释放资源，导致当前线程访问到已释放的资源，从而引发未定义行为。
3. shared_ptr 的引用计数是一个原子操作，但在多线程环境下，对同一个 std::shared_ptr 实例的引用计数的修改可能会引发竞争条件。比如多个线程同时对同一个 std::shared_ptr 进行引用计数的增加或减少操作，可能会导致引用计数不正确，进而导致资源提前释放或内存泄漏等问题。

## 


