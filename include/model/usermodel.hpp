#ifndef USERMODEL_H
#define USERMODEL_H

#include "user.hpp"

//专门操作User表的数据库操作类
class UserModel
{
public:
    // User表的插入方法（注册用到）
    bool insert(User &user);

    // 根据用户号码查询用户信息（登录用到）
    User query(int id);

    // 更新用户的状态信息
    bool updateState(User user);

    // 重置用户的状态信息
    void resetState();
};

#endif // USERMODEL_H