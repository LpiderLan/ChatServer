#ifndef __GROUP_H__
#define __GROUP_H__

#include <string>
#include <vector>
#include "group_user.hpp"

//Group对象包含了两张表的信息_id，_name,_desc来自AllGroup表，_users来自groupuser表
class Group
{
public:
    Group(int id = -1, std::string name = "", std::string desc = "")
        : _id(id),
          _desc(desc),
          _name(name)
    {}

    void setId(int id) { _id = id; }
    void setName(std::string name) { _name = name; }
    void setDesc(std::string desc) { _desc = desc; }

    int getId() { return _id; }
    std::string getName() { return _name; }
    std::string getDesc() { return _desc; }
    std::vector<GroupUser> &getUsers() { return _users; } //返回一个指向 _users 向量的引用，允许外部代码直接访问和修改 _users 向量中的元素

private:
    int _id;
    std::string _name;
    std::string _desc;
    std::vector<GroupUser> _users;
};

#endif // __GROUP_H__