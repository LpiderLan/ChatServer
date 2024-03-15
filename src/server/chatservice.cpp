#include "chatservice.hpp"
#include "public.hpp"
#include <muduo/base/Logging.h>
#include <string>
#include <vector>
using namespace muduo;
using namespace std;

// int getUserId(json& js) { return js["id"].get<int>(); }
// std::string getUserName(json& js) { return js["name"]; }

ChatService::ChatService()
{
    // 对各类消息处理方法的注册
    _msgHandlerMap.insert({LOGIN_MSG, std::bind(&ChatService::loginHandler, this, _1, _2, _3)});
    _msgHandlerMap.insert({REGISTER_MSG, std::bind(&ChatService::registerHandler, this, _1, _2, _3)});
    _msgHandlerMap.insert({ONE_CHAT_MSG, std::bind(&ChatService::oneChatHandler, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_FRIEND_MSG, std::bind(&ChatService::addFriendHandler, this, _1, _2, _3)});
    // 群组业务管理相关事件处理回调注册
    _msgHandlerMap.insert({CREATE_GROUP_MSG, std::bind(&ChatService::createGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_GROUP_MSG, std::bind(&ChatService::addGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({GROUP_CHAT_MSG, std::bind(&ChatService::groupChat, this, _1, _2, _3)});

    if (_redis.connect())
    {
        //保证收到来自redis的消息的时候（publish）的函数调用
        _redis.init_notify_handler(std::bind(&ChatService::redis_subscribe_message_handler, this, _1, _2));
    }
}

// redis订阅消息触发的回调函数,这里channel其实就是id
void ChatService::redis_subscribe_message_handler(int channel, string message)
{
    //用户在线
    lock_guard<mutex> lock(_connMutex);
    auto it = _userConnMap.find(channel); //这里刚好是channel和用户id相等所以可以在_userConnMap中找channel
    if (it != _userConnMap.end())
    {
        it->second->send(message);
        return;
    }

    //转储离线
    _offlineMsgModel.insert(channel, message);
}

MsgHandler ChatService::getHandler(int msgId)
{
    // 找不到对应处理器的情况
    auto it = _msgHandlerMap.find(msgId);
    if (it == _msgHandlerMap.end())
    {
        // 返回一个默认的处理器(lambda匿名函数，仅仅用作提示)
        return [=](const TcpConnectionPtr &conn, json &js, Timestamp) {
            LOG_ERROR << "msgId: " << msgId << " can not find handler!";
        }; 
    }
  
    return _msgHandlerMap[msgId];
}

// 服务器异常，业务重置方法
void ChatService::reset()
{
    // 将所有online状态的用户，设置成offline
    _userModel.resetState();
}


/**
 * 处理客户端异常退出
 */
void ChatService::clientCloseExceptionHandler(const TcpConnectionPtr &conn)
{
    User user;
    // 互斥锁保护
    {
        lock_guard<mutex> lock(_connMutex);
        for (auto it = _userConnMap.begin(); it != _userConnMap.end(); ++it)
        {
            if (it->second == conn) //所有已经连接的客户端中和自己客户端相等的时候
            {
                // 从map表删除用户的链接信息
                user.setId(it->first);  //根据_userConnMap的定义，it->first是当前客户端的连接id也是用户id，所以可以用来设置userid
                _userConnMap.erase(it);
                break;
            }
        }
    }
    
    // 用户注销
    _redis.unsubscribe(user.getId()); 

    // 更新用户的状态信息
    if (user.getId() != -1)
    {
        user.setState("offline");  //将用户的状态设置成offline
        _userModel.updateState(user); //依旧是业务层和数据操作层分离
    }
}

// 一对一聊天业务
void ChatService::oneChatHandler(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    // 需要接收信息的用户ID
    int toId = js["toid"].get<int>();
    
    {
        //要访问连接信息表_userConnMap，所以要锁住
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(toId);
        // 确认是在线状态
        if (it != _userConnMap.end())
        {
            // TcpConnection::send() 直接发送消息
            it->second->send(js.dump());  //A->B中A在js说了什么，B就接收什么，没有改变
            
            return;
        }
    }
    
    // 在数据库找用户是不是在线，如果在线，说明是用户在其他服务器上在线的情况，publish消息到redis
    User user = _userModel.query(toId);
    if (user.getState() == "online")
    {
        _redis.publish(toId, js.dump());
        return;
    }

    // toId 不在线则存储离线消息，也是同样的A在js说了什么，B就接收什么，没有改变
    _offlineMsgModel.insert(toId, js.dump());
}

// 添加朋友业务
void ChatService::addFriendHandler(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userId = js["id"].get<int>();
    int friendId = js["friendid"].get<int>();

    // 存储好友信息
    _friendModel.insert(userId, friendId);  //_friendModel是朋友相关数据库操作的对象
}

// 创建群组业务，包含id，groupname，groupdesc插入到AllGroup表，除此之外，我们还要生成创建者到GroupUser表
void ChatService::createGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userId = js["id"].get<int>();
    std::string name = js["groupname"];
    std::string desc = js["groupesc"];

    // 存储新创建的群组消息
    Group group(-1, name, desc);
    if (_groupModel.createGroup(group))
    {
        // 存储群组创建人信息
        _groupModel.addGroup(userId, group.getId(), "creator"); 
    }
}

// 加入群组业务，插入数据（id，groupid，role）到GroupUser表
void ChatService::addGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userId = js["id"].get<int>();
    int groupId = js["groupid"].get<int>();
    _groupModel.addGroup(userId, groupId, "normal");
}

// 群组聊天业务
void ChatService::groupChat(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userId = js["id"].get<int>();
    int groupId = js["groupid"].get<int>();
    std::vector<int> userIdVec = _groupModel.queryGroupUsers(userId, groupId);  //用户所在群组的所有用户id

    lock_guard<mutex> lock(_connMutex);
    for (int id : userIdVec)
    {
        auto it = _userConnMap.find(id);
        if (it != _userConnMap.end())
        {
            // 转发群消息
            it->second->send(js.dump());
        }
        else
        {
            // 查询toid是否在线
            User user = _userModel.query(id);
            if (user.getState() == "online")
            {
                // 向群组成员publish信息
                _redis.publish(id, js.dump());
            }
            else
            {
                //转储离线消息
                _offlineMsgModel.insert(id, js.dump());
            }
        }
    }
}

/**
 * 登录业务
 * 从json得到用户id
 * 从数据中获取此id的用户，判断此用户的密码是否等于json获取到的密码
 * 判断用户是否重复登录
 * {"msgid":1,"id":13,"password":"123456"}
 * {"errmsg":"this account is using, input another!","errno":2,"msgid":2}
 */
void ChatService::loginHandler(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    LOG_DEBUG << "do login service!";

    int id = js["id"].get<int>();
    std::string password = js["password"];

    User user = _userModel.query(id);
    if (user.getId() == id && user.getPassword() == password)
    {
        if (user.getState() == "online")
        {
            // 该用户已经登录，不能重复登录
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 2;
            response["errmsg"] = "this account is using, input another!";
            conn->send(response.dump());
        }
        else
        {
            // 登录成功，记录用户连接信息
            // 需要考虑线程安全问题 onMessage会在不同线程中被调用
            {
                lock_guard<mutex> lock(_connMutex);
                _userConnMap.insert({id, conn});      // _userConnMap只在这里insert了，表示已经登陆上的用户对应的连接
            }

            // id用户登录成功后，向redis订阅channel(id)
            _redis.subscribe(id); 

            // 登录成功，更新用户状态信息 state offline => online
            user.setState("online");
            _userModel.updateState(user);   //这句话就将数据更新反映到数据库中了，体现了业务模块和数据模块分开的思想

            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 0;
            response["id"] = user.getId();
            response["name"] = user.getName();

            // 查询该用户是否有离线消息
            std::vector<std::string> vec = _offlineMsgModel.query(id);  //_offlineMsgModel是对离线消息数据库操作的模型
            if (!vec.empty())
            {
                response["offlinemsg"] = vec;
                // 读取该用户的离线消息后，将该用户离线消息删除掉
                _offlineMsgModel.remove(id);
            }
            else
            {
                LOG_INFO << "无离线消息";
            }

            // 展示好友信息和状态
            vector<User> userVec = _friendModel.query(id);
            if (!userVec.empty())
            {
                vector<string> vec;   //存储要发送的好友信息
                for (auto& user : userVec)
                {
                    json js;
                    js["id"] = user.getId();
                    js["name"] = user.getName();
                    js["state"] = user.getState();
                    vec.push_back(js.dump()); //js.dump()转成json字符串
                }
                response["friends"] = vec;
            }
            //response包含：用户的id，name 离线消息，朋友
            conn->send(response.dump());
        }
    }
    
}

// 注册业务
// 注册的用户名和id是否和数据库数据重复判断是写在usermodel.cpp中了：id INT: 这是用户的唯一标识符，是主键（PRIMARY KEY）因此 id 字段不允许重复。name VARCHAR(50): 用户名要求为UNIQUE。这意味着每个用户名必须是唯一的，不允许重复。
void ChatService::registerHandler(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    LOG_DEBUG << "do regidster service!";
    //只用设置name和password，其他的参数User的默认构造已经写好了
    std::string name = js["name"];
    std::string password = js["password"];

    User user;
    user.setName(name);
    user.setPassword(password);
    bool state = _userModel.insert(user);  //这句话就将数据生成并插入到数据库中了，体现了业务模块和数据模块分开的思想
    if (state)
    {
        // 注册成功，生成打印的消息并发送回了客户端
        json response;
        response["msgid"] = REGISTER_MSG_ACK;
        response["errno"] = 0;  //表示注册成功了
        response["id"] = user.getId();
        // json::dump() 将序列化信息转换为std::string
        conn->send(response.dump());
    }
    else
    {
        // 注册失败
        json response;
        response["msgid"] = REGISTER_MSG_ACK;
        response["errno"] = 1;
        // 注册已经失败，不需要在json返回id
        conn->send(response.dump());
    }
}

