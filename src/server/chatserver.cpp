#include "chatserver.hpp"
#include "json.hpp"
#include "chatservice.hpp"
#include <iostream>
#include <functional>
#include <string>
using namespace std;
using namespace placeholders;
using json = nlohmann::json;


// 初始化聊天服务器对象
ChatServer::ChatServer(EventLoop *loop,
                       const InetAddress &listenAddr,
                       const string &nameArg)
    : _server(loop, listenAddr, nameArg), _loop(loop)
{
    // 注册连接事件的回调函数
    _server.setConnectionCallback(std::bind(&ChatServer::onConnection, this, _1));  //该函数将在有新连接到达服务器时被调用。

    // 注册消息事件的回调函数
    _server.setMessageCallback(std::bind(&ChatServer::onMessage, this, _1, _2, _3));

    // 设置subLoop线程数量
    _server.setThreadNum(4);
}

// 启动服务
void ChatServer::start()
{
    _server.start();
}

// 连接事件相关信息的回调函数
void ChatServer::onConnection(const TcpConnectionPtr &conn)
{
    // 客户端断开连接或者连接失败
    if (!conn->connected())
    {
        // 处理客户端异常退出事件,这种事件虽然涉及到网络，但是如何处理主要还是业务模块的
        ChatService::instance()->clientCloseExceptionHandler(conn);
        // 半关闭
        conn->shutdown();
    }
}

// 上报读写事件相关信息的回调函数，这个方法会被多个用户调用，也就是说同时有多个连接，每个连接都可能调用不用的业务方法
void ChatServer::onMessage(const TcpConnectionPtr &conn,
                           Buffer *buffer,
                           Timestamp time)
{
    // 将json数据转换为string
    string buf = buffer->retrieveAllAsString();
    // 数据的反序列化
    json js = json::parse(buf);
    
    // 完全解耦网络模块和业务模块，不要在网络模块中调用业务模块的方法（没有出现任何的login，register字眼）
    // 通过 js["msg_id"] 来获取不同的业务处理器（事先绑定的回调方法）
    // js["msgid"].get<int>() 将js["msgid"]对应的值强制转换成int
    auto msgHandler = ChatService::instance()->getHandler(js["msgid"].get<int>());
    // 回调消息绑定好的事件处理器，来执行相应的业务处理
    msgHandler(conn, js, time);
}